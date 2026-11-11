// Enregistrement vidéo, côté jeu : copie de l'image d'un œil dans des textures Direct3D partagées avec
// l'enregistreur de XRPerfSensors (XRPerfRecord.dll), décrites dans le bloc Local\XRPerfOverlay.Video
// (shared_block.h, section 5). Utilisé par la layer (Direct3D 11 ou 12, le périphérique du jeu) et par
// XRPerfVR (image miroir de SteamVR, son propre périphérique Direct3D 11).
//
// Deux temps : copy() copie l'image de l'œil (une zone d'une tranche de la texture du jeu) dans une texture
// partagée libre, publish() la rend visible à l'enregistreur avec ce qu'elle montre (champ de vue...). Dans
// la layer, la copie se fait quand le jeu rend l'image au runtime (xrReleaseSwapchainImage, image encore
// à lui et dans un état connu), la publication à xrEndFrame, qui dit quelle partie montre quel œil.
//
// Règles : rien n'est fait tant que personne n'enregistre (une lecture de mémoire partagée par image) ;
// la copie reste dans la carte graphique et n'attend jamais : si l'enregistreur est en retard, l'image
// est simplement sautée. Toute erreur arrête la copie pour la session, le jeu n'est pas touché.
#pragma once

#include <windows.h>

#include <d3d11.h>
#include <d3d12.h>

#include <cstdint>
#include <memory>
#include <string>

#include "shared_block.h"
#include "video_math.h"

namespace xrpo {

// Zone de la texture du jeu à copier : l'image de l'œil enregistré (sa place exacte est confirmée à la
// publication). Copiée en haut à gauche de la texture partagée.
struct VideoSource {
    void* texture = nullptr;  // ID3D11Texture2D* (Direct3D 11) ou ID3D12Resource* (Direct3D 12)
    uint32_t subresource = 0;
    uint32_t x = 0, y = 0;    // coin de la zone copiée dans la texture du jeu
    uint32_t width = 0, height = 0;  // taille de la zone copiée
};

// Un écran plat du jeu (menu, HUD) à copier avec l'image de l'œil en attente.
struct VideoLayerSource {
    VideoSource source;      // zone copiée en haut à gauche de ses textures partagées
    int64_t viewFormat = 0;  // format de la swapchain (lecture sRGB ou non)
    uint32_t flags = 0;      // kVideoLayerAlpha, kVideoLayerUnpremultiplied (shared_block.h)
    float corners[12] = {};  // coins dans le repère de l'œil (haut gauche, haut droit, bas gauche, bas droit)
};

// Ce que montre l'image copiée.
struct VideoFrameInfo {
    uint32_t x = 0, y = 0, width = 0, height = 0;  // partie de la zone copiée qui contient l'œil
    bool linear = false;  // valeurs linéaires (format non sRGB ou flottant)
    float tanLeft = -1.f, tanRight = 1.f, tanUp = 1.f, tanDown = -1.f;  // champ de cette partie, en tangentes d'angle
    float centerX = 0.f, centerY = 0.f;  // direction de la tête, mêmes unités
    uint32_t panelFlags = 0;   // kVideoPanelShown, kVideoPanelPlaced (shared_block.h)
    float panelCorners[12] = {};  // coins du panneau dans le repère de l'œil, si kVideoPanelPlaced
    bool poseValid = false;    // orientations de l'œil et de la tête dans la scène (stabilisation)
    Quat eyeOrientation, headOrientation;
    uint32_t imageState = 0;   // VideoImageState (shared_block.h)
};

class VideoProducer {
  public:
    // `kind` : kVideoOpenXrD3D11 (périphérique du jeu) ou kVideoSteamVr (périphérique de XRPerfVR).
    static std::unique_ptr<VideoProducer> CreateD3D11(ID3D11Device* device, uint32_t kind);
    static std::unique_ptr<VideoProducer> CreateD3D12(ID3D12Device* device, ID3D12CommandQueue* queue);

    VideoProducer(const VideoProducer&) = delete;
    VideoProducer& operator=(const VideoProducer&) = delete;
    virtual ~VideoProducer();

    // À chaque image : true si un enregistrement attend des images de ce processus. `eye` reçoit l'œil
    // demandé (0 gauche, 1 droit). Sans enregistrement, libère les textures (dès que la carte graphique
    // n'en a plus besoin, sans jamais l'attendre).
    bool active(int& eye);
    // Copie maintenant ? (cadence de la vidéo, à appeler une fois par image du jeu, enregistrement actif).
    bool due(double now);
    // Copie la zone dans une texture libre, sans la publier. Retourne false si rien n'a été copié.
    bool copy(const VideoSource& source);
    bool pending() const { return m_pending >= 0; }
    // Publie la dernière copie avec ce qu'elle montre ; sans effet s'il n'y en a pas.
    void publish(const VideoFrameInfo& info);
    // Oublie la dernière copie (le jeu n'a pas soumis cette image).
    void dropPending() {
        m_pending = -1;
        m_pendingLayerCount = 0;
    }
    // Copie les écrans plats du jeu avec l'image en attente (avant publish) : au plus kVideoLayers, dans
    // l'ordre de la scène ; ceux qui ne peuvent pas être copiés sont omis.
    void copyLayers(const VideoLayerSource* layers, int count);
    // Panneau dans la vidéo demandé par l'enregistreur (VideoOverlayMode) ; kVideoOverlayOff hors enregistrement.
    uint32_t overlayMode() const;
    // Image du panneau pour la vidéo : `width` x `height` pixels RGBA (alpha prémultiplié), lignes de
    // `stride` pixels.
    void publishPanel(const uint8_t* pixels, int width, int height, int stride);
    // Durée de l'enregistrement en cours, pour le témoin du panneau ; négative s'il n'y en a pas.
    double recordingSeconds();
    // Fin de session : rend la main (plus personne ne fournit d'images) et libère les textures.
    void shutdown();
    // Nom du jeu montré dans le nom du fichier (par défaut : l'exécutable de ce processus).
    void setGame(const std::wstring& name);
    // La copie a échoué et ne sera plus tentée dans cette session.
    bool failed() const { return m_failed; }

  protected:
    explicit VideoProducer(uint32_t kind);

    // Spécifique à l'API : textures partagées de la taille de la zone copiée, au format de la texture
    // source, nommées d'après `generation`. Retourne false en cas d'échec (journal écrit).
    virtual bool createResources(const VideoSource& source, uint32_t generation, uint32_t& format) = 0;
    virtual void releaseResources() = 0;
    // Plus aucune copie en cours dans la carte graphique (les textures peuvent être libérées sans attendre).
    virtual bool idle() { return true; }
    // Le slot peut-il être réécrit (copie précédente terminée côté fournisseur) ?
    virtual bool slotReady(int slot) { (void)slot; return true; }
    // Copie la zone dans le slot puis signale `fenceValue` sur le fence partagé.
    virtual bool copyTo(int slot, const VideoSource& source, uint64_t fenceValue) = 0;
    virtual uint64_t adapterLuid() = 0;
    // Format de la texture source (pour savoir s'il faut recréer les textures).
    virtual uint32_t sourceFormat(const VideoSource& source) = 0;
    // Écrans plats : textures partagées de l'écran `layer` (kVideoSlots, nommées d'après `generation`), au
    // format de la source ; libération (aussi faite par releaseResources) ; copie de plusieurs zones dans
    // le slot `slot` de chacun (`layers[i]` : numéro de l'écran de `sources[i]`), puis `fenceValue` signalé.
    virtual bool createLayerResources(int layer, const VideoSource& source, uint32_t generation, uint32_t& format) = 0;
    virtual void releaseLayerResources(int layer) = 0;
    virtual bool copyLayersTo(int slot, const VideoSource* sources, const int* layers, int count, uint64_t fenceValue) = 0;

    const uint32_t m_kind;
    bool m_failed = false;

  private:
    bool openBlock();
    void release(bool keepBlock);
    void fail();
    void writeGame();
    void freeResources();

    std::wstring m_game;
    void* m_mapping = nullptr;
    volatile VideoShareHeader* m_block = nullptr;
    unsigned long long m_lastOpenAttempt = 0;
    bool m_producing = false;      // ce processus a pris le rôle de fournisseur
    uint32_t m_session = 0;        // enregistrement pour lequel les images sont fournies
    FrameThrottle m_throttle;
    bool m_hasResources = false;
    uint32_t m_generation = 0;
    uint32_t m_width = 0, m_height = 0, m_format = 0;
    uint64_t m_frame = 0;          // numéro de la dernière copie (valeur du fence)
    int m_latest = -1;             // slot de la dernière image publiée
    int m_pending = -1;            // slot copié, pas encore publié
    uint64_t m_pendingFrame = 0;
    // Écrans plats : textures de chacun, et ceux copiés avec l'image en attente.
    struct LayerTextures {
        uint32_t generation = 0, width = 0, height = 0, format = 0, viewFormat = 0;
    };
    LayerTextures m_layer[kVideoLayers];
    VideoLayerInfo m_pendingLayers[kVideoLayers] = {};
    int m_pendingLayerCount = 0;
    int m_errors = 0;
};

// Valeurs linéaires (à convertir en sRGB pour la vidéo) selon le format DXGI choisi par le jeu : tout ce
// qui n'est pas un format _SRGB 8 bits (OpenXR : un format UNORM porte des valeurs linéaires).
bool VideoFormatIsLinear(int64_t dxgiFormat);

}  // namespace xrpo
