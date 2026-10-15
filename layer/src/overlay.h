// Logique de l'overlay pour une session OpenXR : mesure de la cadence, rendu du panneau,
// swapchain dédiée et calque "quad" ajouté à la fin de la liste du jeu.
#pragma once

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "config.h"
#include "frame_history.h"
#include "frame_timer.h"
#include "gfx.h"
#include "hand_input.h"
#include "hotkeys.h"
#include "layer_presence.h"
#include "output_tap.h"
#include "panel.h"
#include "panel_share.h"
#include "sensors_reader.h"
#include "session_stats.h"
#include "settings_reader.h"
#include "stats.h"
#include "summary_file.h"
#include "window_share.h"
#include "xr.h"

namespace xrpo {

class Overlay {
  public:
    // La texture de l'overlay contient le panneau en haut et la montre en dessous ; chaque calque
    // n'en montre que sa partie.
    static constexpr int kTextureWidth = Panel::kWidth;
    static constexpr int kTextureHeight = Panel::kHeight + WatchFace::kSize;
    static constexpr int kWatchTop = Panel::kHeight;  // première ligne de la montre dans la texture
    static constexpr int kMaxQuads = 3;               // fenêtre capturée + panneau + montre

    Overlay(const NextFunctions& next, const Config& config, XrInstance instance, std::shared_ptr<HandInput> hands = nullptr);
    ~Overlay();

    XrInstance instance() const { return m_instance; }
    XrSession session() const { return m_session; }

    // Après un xrCreateSession réussi.
    void onSessionCreated(XrSession session, const XrSessionCreateInfo* createInfo);
    // Avant xrDestroySession : libère tout ce que nous avons créé dans la session.
    void onSessionDestroying();

    // Après xrWaitFrame / xrBeginFrame (peuvent venir d'un autre fil que xrEndFrame).
    void onWaitFrame(const XrFrameState& state);
    void onBeginFrame();

    // Au début de chaque xrEndFrame. Remplit `quads` avec nos calques à ajouter (0 à 3) et retourne
    // leur nombre. `displayTime` : instant d'affichage de l'image (pour situer les manettes).
    // `allowLayer` = false : l'image est seulement comptée (image vide soumise par le jeu).
    int onEndFrame(XrCompositionLayerQuad (&quads)[kMaxQuads], XrTime displayTime, bool allowLayer);

    // Après un xrEndFrame accepté par le runtime : relève la résolution de l'image soumise par le jeu.
    // Ne prend aucun verrou.
    void onFrameSubmitted(const XrFrameEndInfo& frameEndInfo) noexcept;

    // Le runtime a refusé une image contenant notre calque.
    void onLayerRejected(XrResult result);
    void onLayerAccepted() { m_rejections = 0; }

  private:
    // Image d'une swapchain : acquise puis « attendue » avant d'y écrire, rendue ensuite au runtime.
    struct SwapImage {
        bool acquired = false;  // une image est acquise et attend d'être remplie
        bool waited = false;    // ... et le runtime a confirmé qu'on peut y écrire
        uint32_t index = 0;
        int waitTimeouts = 0;
    };
    enum class ImageState { Ready, NotYet, Error };
    ImageState prepareImage(XrSwapchain swapchain, SwapImage& image);
    XrResult releaseImage(XrSwapchain swapchain, SwapImage& image);
    int64_t chooseFormat(bool preferBgra, bool& bgra, bool& srgb);

    void pollHotkeys(double now);
    bool ensureResources();
    void destroyResources();
    void sample(double now);
    void pollSettings();
    void uploadPanel();
    void readSystemInfo(XrSystemId systemId);
    std::string m_cpuText, m_gpuText;  // modèles du processeur et de la carte graphique (option)
    void openSummary();
    void writeSummary(double now, bool finished);
    void finishSummary();
    XrPosef quadPose() const;
    // Calque accroché à une main : espace et pose, ou false s'il ne doit pas être affiché maintenant
    // (main non suivie, ou pas regardée en mode « seulement quand je la regarde »).
    enum class HandResult { NotOnHand, Show, Hide };
    HandResult handQuad(Anchor anchor, HandPlacement placement, float along, float lift, bool gazeOnly, XrTime displayTime, bool& looked,
                        XrSpace& space, XrPosef& pose);
    // Même chose avec la pose dans le repère de la poignée déjà calculée (`hand` : 0 gauche, 1 droite).
    HandResult handQuadAt(int hand, const Pose& local, bool gazeOnly, XrTime displayTime, bool& looked, XrSpace& space, XrPosef& pose);

    // Fenêtre capturée (troisième calque).
    void updateWindow(double now, XrTime displayTime);
    bool ensureWindowSwapchain();
    void uploadWindow();
    bool windowQuad(XrTime displayTime, XrCompositionLayerQuad& quad);
    bool ensureWorldSpace();
    bool headInWorld(XrTime displayTime, Pose& head);
    void destroyWindowResources();
    void sampleWatch(double now, const PanelData& data, bool fpsValid, float fps);

    const NextFunctions m_next;
    const XrInstance m_instance;
    Config m_config;
    std::shared_ptr<HandInput> m_hands;  // manettes et batterie ; nul si rien n'est demandé

    std::mutex m_mutex;  // tout l'état ci-dessous sauf la file des horodatages
    XrSession m_session = XR_NULL_HANDLE;
    std::unique_ptr<Gfx> m_gfx;
    XrSpace m_viewSpace = XR_NULL_HANDLE;
    XrSwapchain m_swapchain = XR_NULL_HANDLE;
    bool m_formatIsBgra = false;
    bool m_formatIsSrgb = true;
    bool m_resourcesTried = false;
    bool m_failed = false;         // abandon définitif pour cette session
    SwapImage m_panelImage;        // image de la swapchain du panneau en cours de remplissage
    bool m_hasContent = false;     // au moins une image a été rendue au runtime
    int m_uploadErrors = 0;
    int m_rejections = 0;

    // Fenêtre capturée : image lue dans la mémoire partagée, swapchain à part (taille maximale de
    // l'image, dont seule la partie remplie est montrée), repère de la pièce pour la poser.
    WindowShareReader m_windowShare;
    double m_windowWantedAt = -1.0;          // dernier signal « image voulue » envoyé à XRPerfSensors
    std::vector<uint8_t> m_windowPixels;     // dernière image lue (BGRA)
    std::vector<uint8_t> m_windowConverted;  // la même, dans l'ordre d'octets de la texture
    int m_windowWidth = 0, m_windowHeight = 0;           // taille de l'image lue
    bool m_windowPending = false;                         // image lue pas encore copiée dans la texture
    int m_windowShownWidth = 0, m_windowShownHeight = 0;  // taille de l'image dans la texture
    bool m_windowHasContent = false;
    XrSwapchain m_windowSwapchain = XR_NULL_HANDLE;
    bool m_windowFormatBgra = true, m_windowFormatSrgb = true;
    bool m_windowTried = false;
    bool m_windowFailed = false;      // abandon de la fenêtre pour la session (le panneau reste)
    SwapImage m_windowImage;
    int m_windowErrors = 0;
    bool m_windowLooked = false;
    bool m_windowPlaceRequested = false;  // Ctrl+Alt+G : à poser devant soi dès qu'il y a une image
    XrSpace m_worldSpace = XR_NULL_HANDLE;
    bool m_worldTried = false;
    // Sans espace STAGE, la fenêtre est posée dans l'espace LOCAL, propre à cette session : sa pose n'est
    // alors ni reprise du .ini ni enregistrée.
    bool m_worldIsLocal = false;
    bool m_localWorldSet = false;
    Pose m_localWorld;

    Panel m_panel;
    WatchFace m_watch;
    WatchData m_lastWatch;             // valeurs de la montre au dernier relevé
    bool m_shownWatch = false;         // la texture contient une image de la montre
    bool m_panelLooked = false, m_watchLooked = false;  // dernier résultat du test « regardé ? »
    std::vector<uint8_t> m_texture;    // image complète de la texture (panneau + montre), RGBA sRGB
    std::vector<uint8_t> m_converted;  // la même, convertie dans l'ordre d'octets de la texture
    FrameStats m_stats;
    SensorsReader m_sensors;
    double m_lastSample = -1.0;  // dernier relevé des valeurs (toutes les refresh_ms, panneau visible ou non)
    PanelData m_lastData;        // valeurs du dernier relevé
    bool m_panelDirty = true;    // la texture ne reflète pas encore m_lastData ou les réglages
    Layout m_shownLayout = Layout::Classic;  // présentation de l'image actuellement dans la texture
    bool m_shownInfo = false;                // ... et présence de sa ligne casque / résolution
    bool m_shownStatus = false;              // ... et de sa ligne session / batterie

    FrameHistory m_history;  // temps d'image des dernières secondes (présentation graphique)

    // Mesure du temps GPU (option). m_timingActive est fixé à la création de la session, avant la
    // première image ; il ne repasse ensuite à false (sous m_mutex) que si la mesure est écartée.
    // Atomique : relu sans verrou à xrBeginFrame.
    std::atomic<bool> m_timingActive{false};
    double m_gpuSum = 0.0;             // mesures reçues depuis le dernier relevé
    int m_gpuCount = 0;
    float m_gpuShownMs = 0.f;          // dernière moyenne affichée
    double m_lastGpuMeasure = -1.0;    // instant de la dernière mesure reçue
    int m_timedFrames = 0;             // images soumises depuis l'activation, pour le constat d'échec
    // Mesure invraisemblable (bien plus basse que l'estimation, carte chargée) : depuis quand, et
    // moyennes sur cette période pour le journal et le rapport.
    double m_implausibleSince = -1.0;
    double m_implausibleMeasured = 0.0, m_implausibleEstimate = 0.0, m_implausibleLoad = 0.0;
    int m_implausibleCount = 0;
    void collectGpuTimings();
    void checkGpuPlausibility(PanelData& data, double now);

    // Réglages modifiés en direct depuis la fenêtre de XRPerfSensors.
    SettingsReader m_settingsReader;
    bool m_settingsSeen = false;        // un premier bloc a été lu depuis le début de la session
    unsigned long long m_configTickMs = 0;  // GetTickCount64() à la lecture du .ini (création de la session)
    uint32_t m_settingsGeneration = 0;  // dernier état de réglages pris en compte
    SettingsBlock m_lastBlock = {};      // dernier bloc vu (pour ne reprendre que ce qui a changé)
    bool m_hasLastBlock = false;

    RateMeter m_outRate;  // cadence des images reçues par le runtime (après génération d'images)
    double m_lastOutSample = -1.0;

    // Copie du panneau pour la fenêtre du bureau (seulement quand XRPerfSensors la demande).
    PanelShare m_panelShare;
    std::unique_ptr<Panel> m_desktopPanel;

    // Résumé de session.
    SessionStats m_sessionStats;
    SummaryFile m_summary;
    double m_sessionStart = 0.0;
    double m_lastSummaryWrite = 0.0;
    std::string m_gameText, m_apiText, m_startText;

    // Casque et résolution de rendu.
    std::string m_headsetText;  // nom affiché (celui du .ini s'il est renseigné, sinon celui du runtime)
    std::string m_systemText;   // nom annoncé par le runtime
    int m_recommendedWidth = 0;  // résolution par œil recommandée par le runtime (0 = inconnue)
    int m_recommendedHeight = 0;
    std::string m_runtimeText;  // runtime OpenXR et sa version
    std::atomic<uint64_t> m_renderSize{0};  // largeur << 32 | hauteur de l'image d'un œil, 0 = inconnue
    Hotkeys m_hotkeys;
    // Signal pour XRPerfVR (jeux SteamVR) : la layer mesure et affiche dans ce processus.
    LayerPresence m_presence;

    std::mutex m_timingMutex;
    FrameTimer m_timer;  // protégé par m_timingMutex
};

}  // namespace xrpo
