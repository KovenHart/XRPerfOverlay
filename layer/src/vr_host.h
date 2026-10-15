// XRPerfVR : panneau, montre, rapports de session et miroir sur le bureau pour les jeux SteamVR
// (OpenVR), qui ne passent pas par OpenXR et ne chargent donc pas la layer.
//
// Le programme est une application « overlay » de SteamVR : il ne touche pas au jeu. Il attend que
// SteamVR soit lancé (il ne le démarre jamais), lit les temps d'image que le compositeur de SteamVR
// relève pour le jeu au premier plan (cadence, temps CPU, temps GPU réellement mesuré), et affiche les
// mêmes calques que la layer, avec les mêmes réglages (XRPerfOverlay.ini et fenêtre de réglages).
//
// Un jeu OpenXR lancé à travers SteamVR est aussi vu par SteamVR : quand la layer y est active
// (layer_presence.h), XRPerfVR la laisse faire et n'ajoute ni calque ni rapport.
#pragma once

#include <memory>
#include <string>

#include "config.h"
#include "frame_history.h"
#include "hotkeys.h"
#include "panel.h"
#include "panel_share.h"
#include "sensors_reader.h"
#include "session_stats.h"
#include "settings_reader.h"
#include "stats.h"
#include "summary_file.h"
#include "vr_backend.h"
#include "window_share.h"

namespace xrpo {

class VrHost {
  public:
    // `iniFolder` : dossier de XRPerfOverlay.ini (celui de la layer). `afterQuit` : le programme est
    // relancé juste après une fermeture de SteamVR, qui peut ne pas être terminée.
    VrHost(VrBackend& vr, std::wstring iniFolder, bool afterQuit = false);
    ~VrHost();
    VrHost(const VrHost&) = delete;
    VrHost& operator=(const VrHost&) = delete;

    // À appeler en boucle, toutes les 20 ms environ.
    void tick();
    // Fin du programme : rapport terminé, calques retirés, déconnexion de SteamVR.
    void stop();

    bool connected() const { return m_state == State::Connected; }
    // SteamVR a demandé aux applications de quitter (VREvent_Quit) et le programme s'y est engagé :
    // il doit maintenant se terminer (XRPerfSensors le relance aussitôt, en attente du prochain SteamVR).
    bool quitAcknowledged() const { return m_quitAcknowledged; }
    bool inSession() const { return m_session.active; }

  private:
    enum class State { WaitRuntime, Connected, WaitRuntimeExit };

    struct Session {
        bool active = false;
        uint32_t pid = 0;
        bool report = false;   // XRPerfVR écrit le rapport (la layer ne mesure pas ce jeu)
        bool display = false;  // XRPerfVR affiche les calques (la layer ne le fait pas)
        bool mirror = false;   // XRPerfVR alimente la fenêtre du bureau
    };

    void leave(double now);
    void run(double now);
    void watchScene(double now);
    void beginSession(uint32_t pid, double now);
    void endSession(double now);
    void checkLayer(double now);
    void readFrames(double now);
    void pollSettings();
    void sample(double now);
    void sampleWatch(double now, const PanelData& data, bool fpsValid, float fps);
    void display(double now);
    void hideAll();
    // Place et montre (ou masque) un calque selon son accroche. Retourne true s'il est visible.
    bool placeOverlay(VrOverlay overlay, Anchor anchor, HandPlacement placement, float along, float lift, bool gazeOnly,
                      const Pose& viewPose, bool& looked, double now);
    // Fenêtre capturée : sur une main, sinon posée dans l'espace. Retourne true si elle est visible.
    bool placeWindow(double now);
    bool sendPlacement(VrOverlay overlay, int kind, int hand, const Pose& pose, double now);
    void updateWindowImage(double now);
    void upload(VrOverlay overlay);
    void setShown(VrOverlay overlay, bool shown);
    void openSummary();
    void writeSummary(double now, bool finished);
    void finishSummary(double now);

    VrBackend& m_vr;
    const std::wstring m_iniFolder;
    State m_state = State::WaitRuntime;
    double m_nextRuntimeCheck = 0.0;
    double m_runtimeSince = -1.0;     // SteamVR vu lancé depuis (on lui laisse le temps de démarrer)
    double m_waitExitSince = 0.0;     // attente de la fin de SteamVR commencée à (horloge de now())
    bool m_overlaysReady = false;
    double m_nextOverlayAttempt = 0.0;  // nouvel essai de création des calques refusés
    bool m_quitAcknowledged = false;
    VrSystemInfo m_info;

    // Jeu au premier plan.
    double m_nextSceneCheck = 0.0;
    uint32_t m_candidatePid = 0;      // dernier processus vu au premier plan
    double m_candidateSince = 0.0;
    uint32_t m_skippedPid = 0;        // processus écarté (exclu dans le .ini, ou la layer s'en occupe)
    bool m_skippedForLayer = false;   // ... parce que la layer y fait tout : revérifié régulièrement
    double m_sceneLostSince = -1.0;   // le jeu suivi n'est plus au premier plan depuis
    double m_nextLayerCheck = 0.0;
    Session m_session;

    // Images du compositeur.
    bool m_framesPrimed = false;      // l'historique d'avant la connexion a été écarté
    uint32_t m_lastIndex = 0;
    double m_lastFrameTime = 0.0;
    double m_clockOffset = 0.0;       // horloge de SteamVR -> horloge du programme
    double m_windowMinOffset = 0.0;
    double m_offsetWindowStart = -1.0;

    // Mesures (comme la layer).
    Config m_config;
    unsigned long long m_configTickMs = 0;
    SettingsReader m_settingsReader;
    bool m_settingsSeen = false;
    uint32_t m_settingsGeneration = 0;
    SettingsBlock m_lastBlock = {};
    bool m_hasLastBlock = false;
    FrameStats m_stats;
    FrameHistory m_history;
    SessionStats m_sessionStats;
    SensorsReader m_sensors;
    Hotkeys m_hotkeys;
    double m_gpuSum = 0.0;
    int m_gpuCount = 0;
    float m_gpuShownMs = 0.f;
    double m_lastGpuMeasure = -1.0;
    double m_lastSample = -1.0;
    PanelData m_lastData;
    WatchData m_lastWatch;
    bool m_dirty = true;              // un nouveau relevé (ou de nouveaux réglages) est à dessiner
    bool m_overlayDirty[3] = {true, true, true};  // ... pas encore envoyé à ce calque (envoi seulement s'il est visible)

    // Affichage.
    Panel m_panel;
    WatchFace m_watch;
    bool m_hasPixels[3] = {false, false, false};
    bool m_shown[3] = {false, false, false};
    bool m_looked[3] = {false, false, false};
    // Dernière position envoyée à SteamVR, renvoyée seulement si elle change (ou toutes les 2 s).
    struct Placement {
        int kind = 0;  // 0 : aucune, 1 : tête, 2 : main, 3 : pièce
        int hand = -1;
        Pose pose;
        double sentAt = -1.0;
    };
    Placement m_placement[3];

    // Fenêtre capturée (image lue dans la mémoire partagée de XRPerfSensors).
    WindowShareReader m_windowShare;
    double m_windowWantedAt = -1.0;
    std::vector<uint8_t> m_windowPixels;
    int m_windowWidth = 0, m_windowHeight = 0;
    bool m_windowPending = false;           // image lue pas encore envoyée à SteamVR
    float m_windowSentWidth = -1.f;         // largeur (m) envoyée à SteamVR
    bool m_windowPlaceRequested = false;    // Ctrl+Alt+G : à poser devant soi dès qu'il y a une image
    uint32_t m_imageGeneration = 0;         // VrBackend::imageGeneration() au dernier passage
    std::unique_ptr<PanelShare> m_panelShare;
    std::unique_ptr<Panel> m_desktopPanel;

    // Rapport de session.
    SummaryFile m_summary;
    double m_sessionStart = 0.0;
    double m_lastSummaryWrite = 0.0;
    std::string m_gameText, m_startText, m_headsetText, m_cpuText, m_gpuText;
};

}  // namespace xrpo
