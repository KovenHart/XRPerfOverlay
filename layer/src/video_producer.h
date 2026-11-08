// Enregistrement vidéo, côté jeu : copie de l'image d'un œil dans des textures Direct3D partagées avec
// l'enregistreur de XRPerfSensors (XRPerfRecord.dll), décrites dans le bloc Local\XRPerfOverlay.Video
// (shared_block.h, section 5). Utilisé par la layer (Direct3D 11 ou 12, le périphérique du jeu) et par
// XRPerfVR (image miroir de SteamVR, son propre périphérique Direct3D 11).
//
// Deux temps : copy() copie une texture du jeu entière (une tranche) dans une texture partagée libre,
// publish() la rend visible à l'enregistreur avec la partie qui contient l'œil et son champ de vue. Dans
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

// Texture du jeu à copier (entière : la partie utile est donnée à la publication).
struct VideoSource {
    void* texture = nullptr;  // ID3D11Texture2D* (Direct3D 11) ou ID3D12Resource* (Direct3D 12)
    uint32_t subresource = 0;
    uint32_t x = 0, y = 0;    // coin de la zone copiée (0, 0 pour toute la tranche)
    uint32_t width = 0, height = 0;  // taille de la zone copiée
};

// Ce que montre l'image copiée.
struct VideoFrameInfo {
    uint32_t x = 0, y = 0, width = 0, height = 0;  // partie de la zone copiée qui contient l'œil
    bool linear = false;  // valeurs linéaires (format non sRGB ou flottant)
    float tanLeft = -1.f, tanRight = 1.f, tanUp = 1.f, tanDown = -1.f;  // champ de cette partie, en tangentes d'angle
    float centerX = 0.f, centerY = 0.f;  // direction de la tête, mêmes unités
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
    void dropPending() { m_pending = -1; }
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

    const uint32_t m_kind;
    bool m_failed = false;

  private:
    bool openBlock();
    void release(bool keepBlock);
    void fail();
    void writeGame();

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
    int m_errors = 0;
};

// Valeurs linéaires (à convertir en sRGB pour la vidéo) selon le format DXGI choisi par le jeu : tout ce
// qui n'est pas un format _SRGB 8 bits (OpenXR : un format UNORM porte des valeurs linéaires).
bool VideoFormatIsLinear(int64_t dxgiFormat);

}  // namespace xrpo
