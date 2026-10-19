#include "vr_host.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "ascii.h"
#include "hardware_info.h"
#include "log.h"
#include "settings_apply.h"
#include "version.h"
#include "view_pose.h"

namespace xrpo {

namespace {

const double kRuntimeCheckSeconds = 2.0;    // recherche de SteamVR, sans connexion
const double kRuntimeSettleSeconds = 5.0;   // SteamVR vu lancé depuis au moins ce temps avant de s'y connecter
const double kWaitExitMaxSeconds = 20.0;    // attente de la fin de SteamVR, au plus
const double kConnectRetrySeconds = 30.0;   // après un échec de connexion
const double kSceneCheckSeconds = 0.5;      // jeu au premier plan
const double kSceneGraceSeconds = 2.0;      // un nouveau jeu est suivi après ce délai (la layer a le temps de se signaler)
const double kLayerCheckSeconds = 1.0;      // la layer OpenXR est-elle apparue dans le jeu suivi ?
const double kOffsetWindowSeconds = 10.0;   // recalage de l'horloge de SteamVR
const double kSceneLostSeconds = 2.0;       // jeu absent du premier plan depuis ce temps : fin de la session
const double kOverlayRetrySeconds = 30.0;   // nouvel essai de création des calques
const double kPlacementRefreshSeconds = 2.0;
const unsigned long long kSettingsGraceMs = 2000;
const int kMaxFrames = 32;

// Programmes de SteamVR lui-même, à ne pas suivre comme des jeux.
bool IsSteamVrShell(const std::string& lowered) { return lowered == "steamtours.exe" || lowered == "vrdashboard.exe"; }

bool SamePose(const Pose& a, const Pose& b) {
    return a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z && a.orientation.x == b.orientation.x &&
           a.orientation.y == b.orientation.y && a.orientation.z == b.orientation.z && a.orientation.w == b.orientation.w;
}

std::wstring Widen(const std::string& text) { return std::wstring(text.begin(), text.end()); }

std::string Lower(std::string text) {
    for (char& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

}  // namespace

VrHost::VrHost(VrBackend& vr, std::wstring iniFolder, bool afterQuit) : m_vr(vr), m_iniFolder(std::move(iniFolder)) {
    if (afterQuit) {
        m_state = State::WaitRuntimeExit;
        m_waitExitSince = m_vr.now();
    }
}

VrHost::~VrHost() { stop(); }

void VrHost::stop() {
    if (m_state == State::Connected) leave(m_vr.now());
    m_state = State::WaitRuntime;
}

void VrHost::tick() {
    const double now = m_vr.now();
    switch (m_state) {
        case State::WaitRuntime: {
            if (now < m_nextRuntimeCheck) return;
            m_nextRuntimeCheck = now + kRuntimeCheckSeconds;
            if (!m_vr.runtimeRunning()) {
                m_runtimeSince = -1.0;
                return;
            }
            if (m_runtimeSince < 0.0) m_runtimeSince = now;
            if (now - m_runtimeSince < kRuntimeSettleSeconds) return;
            if (!m_vr.connect()) {
                m_nextRuntimeCheck = now + kConnectRetrySeconds;
                return;
            }
            m_state = State::Connected;
            m_info = m_vr.systemInfo();
            m_overlaysReady = m_vr.createOverlays();
            m_nextOverlayAttempt = now + kOverlayRetrySeconds;
            m_windowSentWidth = -1.f;
            m_windowShare.resetFrame();  // calques neufs : la dernière image de la fenêtre est à renvoyer
            m_framesPrimed = false;
            m_offsetWindowStart = -1.0;
            m_candidatePid = 0;
            m_skippedPid = 0;
            m_skippedForLayer = false;
            m_sceneLostSince = -1.0;
            m_nextSceneCheck = now;
            m_nextRuntimeCheck = now + kRuntimeCheckSeconds;
            for (int i = 0; i < static_cast<int>(VrOverlay::Count); ++i) {
                m_shown[i] = m_hasPixels[i] = false;
                m_overlayDirty[i] = true;
                m_placement[i] = Placement();
            }
            Log("XRPerfVR : connecte a %s (casque \"%s\", %.0f Hz, rendu recommande %d x %d)%s.", m_info.runtime.c_str(), m_info.headset.c_str(),
                static_cast<double>(m_info.refreshHz), m_info.recommendedWidth, m_info.recommendedHeight,
                m_overlaysReady ? "" : " ; calques refuses par SteamVR, rapports seulement");
            return;
        }
        case State::WaitRuntimeExit:
            // SteamVR se ferme : on attend qu'il soit vraiment parti, sinon une reconnexion le garderait ouvert.
            if (now < m_nextRuntimeCheck) return;
            m_nextRuntimeCheck = now + kRuntimeCheckSeconds;
            if (!m_vr.runtimeRunning()) {
                m_state = State::WaitRuntime;
                m_runtimeSince = -1.0;
                return;
            }
            // SteamVR relancé si vite que son départ n'a pas été vu (fin de la configuration de la pièce,
            // redémarrage demandé par SteamVR) : son serveur est plus récent que le début de l'attente.
            // Par sécurité, une fermeture qui dure plus de 20 s n'en est plus une : SteamVR est de nouveau attendu.
            {
                const double age = m_vr.runtimeAge();
                const bool restarted = age >= 0.0 && age < now - m_waitExitSince;
                if (restarted || now - m_waitExitSince >= kWaitExitMaxSeconds) {
                    Log(restarted ? "XRPerfVR : SteamVR relance." : "XRPerfVR : SteamVR toujours present, nouvel essai de connexion.");
                    m_state = State::WaitRuntime;
                    m_runtimeSince = restarted ? now - age : now;  // délai de démarrage compté depuis son lancement
                }
            }
            return;
        case State::Connected: {
            const bool quit = m_vr.quitRequested();  // SteamVR se ferme et l'a demandé
            bool lost = quit;
            if (!lost && now >= m_nextRuntimeCheck) {
                m_nextRuntimeCheck = now + kRuntimeCheckSeconds;
                lost = !m_vr.runtimeRunning();  // SteamVR arrêté sans prévenir
            }
            if (lost) {
                leave(now);
                m_state = State::WaitRuntimeExit;
                m_waitExitSince = now;
                m_nextRuntimeCheck = now + kRuntimeCheckSeconds;
                if (quit) m_quitAcknowledged = true;
                return;
            }
            // Calques refusés à la connexion (clé encore prise par une instance qui se terminait...) : nouvel essai.
            if (!m_overlaysReady && now >= m_nextOverlayAttempt) {
                m_nextOverlayAttempt = now + kOverlayRetrySeconds;
                m_overlaysReady = m_vr.createOverlays();
                if (m_overlaysReady) Log("XRPerfVR : calques crees au nouvel essai.");
            }
            run(now);
            return;
        }
    }
}

void VrHost::leave(double now) {
    endSession(now);
    if (m_overlaysReady) m_vr.destroyOverlays();
    m_overlaysReady = false;
    m_vr.disconnect();
    Log("XRPerfVR : deconnecte de SteamVR.");
}

void VrHost::run(double now) {
    watchScene(now);
    readFrames(now);
    if (!m_session.active) return;

    // Raccourcis clavier : seulement quand XRPerfVR affiche (sinon la layer s'en charge).
    if (m_session.display) {
        const unsigned changes = m_hotkeys.poll(m_config, now);
        if (changes & Hotkeys::ResetStats) m_stats.reset();
        if (changes & (Hotkeys::Visibility | Hotkeys::Opacity | Hotkeys::Size)) m_dirty = true;
        if (changes & Hotkeys::WindowPlace) m_windowPlaceRequested = true;
        if ((changes & Hotkeys::WindowVisibility) && !m_config.window.enabled) m_windowPlaceRequested = false;
        if (m_hotkeys.saveDue(now)) SaveState(m_config);
    }
    if (m_lastSample < 0.0 || (now - m_lastSample) * 1000.0 >= m_config.refreshMs) sample(now);
    // Rapport : seulement si XRPerfVR l'écrit (sinon la layer relève elle-même le raccourci).
    if (m_session.report) {
        const ReportControl::Step step = m_reportControl.update(m_config.reportMode, m_recording, Hotkeys::comboDown(m_config.keyRecord));
        if (step.stop) stopRecording(now);
        if (step.start) startRecording(now, step.manual);
    }
    if (m_session.report && m_summary.isOpen() && now - m_lastSummaryWrite >= kReportPeriod &&
        m_sessionStats.activeSeconds() >= (m_recordManual ? kManualReportMinSeconds : kReportMinSeconds))
        writeSummary(now, false);
    display(now);
}

// Jeu au premier plan de SteamVR : début et fin des sessions.
void VrHost::watchScene(double now) {
    if (now < m_nextSceneCheck) return;
    m_nextSceneCheck = now + kSceneCheckSeconds;
    const uint32_t pid = m_vr.sceneProcess();
    if (pid != m_candidatePid) {
        m_candidatePid = pid;
        m_candidateSince = now;
        if (pid != m_skippedPid) {
            m_skippedPid = 0;
            m_skippedForLayer = false;
        }
    }
    // Le jeu suivi quitte le premier plan : la session se termine s'il ne revient pas vite (le tableau
    // de bord de SteamVR ou un court passage à vide ne coupent pas le rapport en deux).
    if (m_session.active) {
        if (m_session.pid == pid) {
            m_sceneLostSince = -1.0;
        } else {
            if (m_sceneLostSince < 0.0) m_sceneLostSince = now;
            if (pid != 0 || now - m_sceneLostSince >= kSceneLostSeconds) endSession(now);
        }
    }
    // Jeu laissé à la layer : si elle cesse d'afficher (abandon pour la session), XRPerfVR prend le relais.
    if (m_skippedForLayer && pid == m_skippedPid && now >= m_nextLayerCheck) {
        m_nextLayerCheck = now + kLayerCheckSeconds;
        if (!(m_vr.layerMeasures(pid) && m_vr.layerDisplays(pid))) {
            m_skippedPid = 0;
            m_skippedForLayer = false;
        }
    }
    if (!m_session.active && pid != 0 && pid != m_skippedPid && now - m_candidateSince >= kSceneGraceSeconds) beginSession(pid, now);
    if (m_session.active && now >= m_nextLayerCheck) checkLayer(now);
}

void VrHost::beginSession(uint32_t pid, double now) {
    const std::string name = m_vr.processName(pid);
    m_config = LoadConfigFromFolder(m_iniFolder);
    m_configTickMs = GetTickCount64();
    if (IsSteamVrShell(Lower(name))) {
        m_skippedPid = pid;  // SteamVR Home : pas un jeu
        return;
    }
    const std::wstring lowered = Widen(Lower(name));
    for (const std::wstring& excluded : m_config.exclude) {
        if (!lowered.empty() && lowered == excluded) {
            m_skippedPid = pid;
            Log("XRPerfVR : \"%s\" est exclu dans le .ini : ignore.", name.c_str());
            return;
        }
    }
    const bool measures = m_vr.layerMeasures(pid), displays = m_vr.layerDisplays(pid);
    if (measures && displays) {
        m_skippedPid = pid;  // jeu OpenXR : la layer y fait tout
        m_skippedForLayer = true;
        m_nextLayerCheck = now + kLayerCheckSeconds;
        Log("XRPerfVR : \"%s\" passe par OpenXR et la layer s'en occupe.", name.c_str());
        return;
    }

    m_session = Session();
    m_session.active = true;
    m_session.pid = pid;
    m_session.report = !measures;
    m_session.display = !displays;
    m_session.mirror = !measures;  // la layer publie elle-même le panneau sur le bureau quand elle mesure

    m_info = m_vr.systemInfo();  // la fréquence ou la résolution ont pu changer depuis la connexion
    m_stats.setWindow(m_config.statsWindowS);
    m_stats.reset();
    m_history.reset();
    m_sessionStats.reset();
    m_hotkeys = Hotkeys();
    m_settingsSeen = false;
    m_hasLastBlock = false;
    m_gpuSum = 0.0;
    m_gpuCount = 0;
    m_gpuShownMs = 0.f;
    m_lastGpuMeasure = -1.0;
    m_lastSample = -1.0;
    m_lastData = PanelData();
    m_lastWatch = WatchData();
    m_looked[0] = m_looked[1] = m_looked[2] = false;
    m_sceneLostSince = -1.0;
    m_dirty = true;
    m_overlayDirty[0] = m_overlayDirty[1] = m_overlayDirty[2] = true;
    m_windowPending = false;
    m_windowPlaceRequested = false;
    m_windowWantedAt = -1.0;
    m_windowShare.resetFrame();  // la dernière image est relue au début de chaque jeu
    m_sessionStart = now;
    m_lastSummaryWrite = now;
    m_nextLayerCheck = now + kLayerCheckSeconds;
    m_gameText = name.empty() ? "?" : name;
    m_headsetText = m_config.headsetName.empty() ? m_info.headset : m_config.headsetName;
    m_cpuText.clear();
    m_gpuText.clear();
    if (m_config.summaryHardware) {
        m_cpuText = CpuModelName();
        m_gpuText = MainGpuName();
    }
    m_recording = m_recordManual = false;
    if (m_reportControl.begin(m_config.reportMode, Hotkeys::comboDown(m_config.keyRecord)) && m_session.report) startRecording(now, false);
    if (m_session.mirror) m_panelShare.reset(new PanelShare());
    Log("XRPerfVR : jeu \"%s\" (processus %lu) : %s, %s.", m_gameText.c_str(), static_cast<unsigned long>(pid),
        m_session.display ? (m_overlaysReady ? "panneau et montre affiches par XRPerfVR" : "calques indisponibles")
                          : "affichage laisse a la layer OpenXR",
        m_session.report ? "rapport ecrit par XRPerfVR" : "rapport laisse a la layer OpenXR");
}

void VrHost::endSession(double now) {
    if (!m_session.active) return;
    if (m_hotkeys.takePending()) SaveState(m_config);
    stopRecording(now);
    finishSummary(now);
    hideAll();
    m_panelShare.reset();  // la fenêtre du bureau sait tout de suite que le jeu est terminé
    Log("XRPerfVR : fin du jeu \"%s\".", m_gameText.c_str());
    m_session = Session();
}

// La layer OpenXR a pu se signaler après le début du suivi (jeu OpenXR lent à créer sa session) :
// elle reprend ce qu'elle fait elle-même.
void VrHost::checkLayer(double now) {
    m_nextLayerCheck = now + kLayerCheckSeconds;
    const bool measures = m_vr.layerMeasures(m_session.pid), displays = m_vr.layerDisplays(m_session.pid);
    if (measures && displays) {
        const uint32_t pid = m_session.pid;
        Log("XRPerfVR : la layer OpenXR a pris le relais dans \"%s\".", m_gameText.c_str());
        endSession(now);  // rapport provisoire (moins de 15 s) : supprimé ; sinon terminé ici
        m_skippedPid = pid;
        m_skippedForLayer = true;
        return;
    }
    if (measures && m_session.report) {
        stopRecording(now);
        finishSummary(now);
        m_session.report = false;
        m_session.mirror = false;
        m_panelShare.reset();
    }
    if (displays && m_session.display) {
        hideAll();
        m_session.display = false;
    }
}

// Nouvelles images relevées par le compositeur depuis l'appel précédent.
void VrHost::readFrames(double now) {
    VrFrame frames[kMaxFrames];
    const int count = std::min(m_vr.frameTimings(frames, kMaxFrames), kMaxFrames);
    if (count <= 0) return;
    const VrFrame& latest = frames[count - 1];

    // Horloge de SteamVR -> la nôtre. La dernière image vise l'affichage en cours ou le prochain :
    // l'écart le plus petit observé sur quelques secondes est la meilleure estimation, recalée
    // régulièrement pour suivre une éventuelle dérive.
    const double offset = now - latest.vsyncSeconds;
    if (m_offsetWindowStart < 0.0) {
        m_clockOffset = m_windowMinOffset = offset;
        m_offsetWindowStart = now;
    } else {
        m_windowMinOffset = std::min(m_windowMinOffset, offset);
        if (offset < m_clockOffset) m_clockOffset = offset;
        if (now - m_offsetWindowStart >= kOffsetWindowSeconds) {
            m_clockOffset = m_windowMinOffset;
            m_windowMinOffset = offset;
            m_offsetWindowStart = now;
        }
    }

    // Numérotation repartie de zéro (compositeur de SteamVR relancé) : on repart de la dernière image,
    // comme à la connexion.
    if (m_framesPrimed && latest.index < m_lastIndex) m_framesPrimed = false;
    if (!m_framesPrimed) {
        // Historique d'avant la connexion (ou d'avant la reprise de la numérotation) : écarté.
        m_framesPrimed = true;
        m_lastIndex = latest.index > 0 ? latest.index - 1 : 0;
        m_lastFrameTime = latest.vsyncSeconds + m_clockOffset;
        return;
    }
    // La dernière image est en général encore en cours : ses temps CPU et GPU ne sont pas encore
    // connus. Elle sera lue au passage suivant, une fois terminée.
    for (int i = 0; i < count - 1; ++i) {
        const VrFrame& f = frames[i];
        if (f.index <= m_lastIndex) continue;
        m_lastIndex = f.index;
        double t = f.vsyncSeconds + m_clockOffset;
        if (t <= m_lastFrameTime) t = m_lastFrameTime + 1e-6;
        m_lastFrameTime = t;
        if (!m_session.active || t < m_sessionStart) continue;
        m_stats.onFrame(t, f.cpuMs);
        m_sessionStats.onFrame(t, f.cpuMs);
        m_history.onFrame(t, f.cpuMs);
        if (!std::isnan(f.gpuMs)) {
            m_history.onGpuFrame(t, f.gpuMs);
            m_sessionStats.onGpuFrame(t, f.gpuMs);
            m_gpuSum += f.gpuMs;
            ++m_gpuCount;
            m_lastGpuMeasure = t;
        }
    }
}

// Réglages publiés par la fenêtre de XRPerfSensors : même règle que la layer.
void VrHost::pollSettings() {
    SettingsBlock block;
    if (!m_settingsReader.read(block)) return;
    if (!m_settingsSeen) {
        m_settingsSeen = true;
        m_settingsGeneration = block.generation;
        const bool changedSinceIni = block.changeTickMs != 0 && block.changeTickMs + kSettingsGraceMs > m_configTickMs;
        if (!changedSinceIni) {
            m_lastBlock = block;
            m_hasLastBlock = true;
            return;
        }
        m_hasLastBlock = false;
    } else if (block.generation == m_settingsGeneration) {
        return;
    }
    m_settingsGeneration = block.generation;
    ApplySettingsUpdate(m_config, block, m_hasLastBlock ? &m_lastBlock : nullptr);
    m_lastBlock = block;
    m_hasLastBlock = true;
    m_dirty = true;
}

void VrHost::sample(double now) {
    pollSettings();
    PanelData data;
    const StatsSnapshot stats = m_stats.snapshot(now);
    data.fpsValid = stats.valid;
    data.fps = stats.fps;
    data.fpsAvg = stats.avg;
    data.fpsMin = stats.min;
    data.fpsMax = stats.max;
    data.cpuFrameMs = stats.cpuMs;
    data.targetFps = std::round(m_info.refreshHz);
    SensorBlock sensors;
    if (m_sensors.read(sensors)) {
        data.sensorsOk = true;
        data.cpuLoad = sensors.cpuLoad;
        data.cpuTemp = sensors.cpuTemp;
        data.cpuMHz = sensors.cpuMHz;
        data.gpuLoad = sensors.gpuLoad;
        data.gpuTemp = sensors.gpuTemp;
        data.gpuMHz = sensors.gpuMHz;
        data.vramUsedMB = sensors.vramUsedMB;
        data.vramTotalMB = sensors.vramTotalMB;
        if (stats.valid && stats.fps > 0.f && !std::isnan(sensors.gpuLoad))
            data.gpuFrameEstimateMs = (sensors.gpuLoad / 100.f) * (1000.f / stats.fps);
    }
    // Temps GPU : celui que SteamVR mesure pour le jeu, image par image. Sans mesure récente
    // (compositeur muet), l'estimation tirée de la charge de la carte.
    if (m_gpuCount > 0) {
        m_gpuShownMs = static_cast<float>(m_gpuSum / m_gpuCount);
        m_gpuSum = 0.0;
        m_gpuCount = 0;
    }
    if (m_lastGpuMeasure >= 0.0 && now - m_lastGpuMeasure < 1.0) {
        data.gpuFrameMs = m_gpuShownMs;
        data.gpuMeasured = true;
    } else {
        data.gpuFrameMs = data.gpuFrameEstimateMs;
        if (m_lastSample >= 0.0) m_history.onGpuEstimate(m_lastSample, now, data.gpuFrameEstimateMs);
    }
    if (m_config.clock) data.clockMinutes = LocalClockMinutes();

    // Casque et résolution : en OpenVR, la taille de l'image du jeu n'est pas visible ; la ligne
    // montre celle que SteamVR recommande.
    data.showInfo = m_config.headsetInfo;
    const size_t nameLength = std::min(m_headsetText.size(), sizeof(data.headset) - 1);
    std::memcpy(data.headset, m_headsetText.data(), nameLength);
    data.headset[nameLength] = '\0';
    data.recommendedWidth = m_info.recommendedWidth;
    data.recommendedHeight = m_info.recommendedHeight;

    // Ligne session / batterie (option) : batterie lue auprès de SteamVR.
    data.statusElements = m_config.panelElements;
    data.sessionSeconds = now - m_sessionStart;
    data.recordState = m_reportControl.indicator(now, m_recording && m_recordManual, m_recordStart, data.recordSeconds);
    // Rapport écrit par la layer (jeu OpenXR en Vulkan ou OpenGL) : son témoin, sur le panneau d'XRPerfVR.
    if (!m_session.report) {
        int state = 0;
        float seconds = 0.f;
        if (m_vr.layerRecord(m_session.pid, state, seconds) && state >= kRecordNone && state <= kRecordTooShort) {
            data.recordState = state;
            data.recordSeconds = seconds;
        }
    }
    if (data.statusElements & kWatchBattery) {
        for (int hand = 0; hand < 2; ++hand) {
            float level = 0.f;
            bool charging = false;
            if (m_vr.battery(hand, level, charging) && level >= 0.f && level <= 1.f) {
                data.battery[hand] = level;
                data.charging[hand] = charging;
            }
        }
    }

    data.layout = m_config.layout;
    data.theme = ThemeOf(m_config);
    data.english = m_config.english;
    if (data.layout == Layout::Graph) {
        m_history.columns(now, data.graphFrameMs, data.graphCpuMs, data.graphGpuMs);
        data.graphValid = true;
    }

    m_sessionStats.onSample(data, stats.fullSecond && now - m_recordStart >= 1.0, now);
    if (m_config.watch.enabled) sampleWatch(now, data, stats.valid, stats.fps);

    if (m_panelShare && m_panelShare->wanted()) {
        if (!m_desktopPanel) m_desktopPanel.reset(new Panel());
        m_desktopPanel->render(data, 1.f, 1.f);
        m_panelShare->publish(m_desktopPanel->pixels(), Panel::contentHeight(data));
    }
    m_lastData = data;
    m_lastSample = now;
    m_dirty = true;
}

void VrHost::sampleWatch(double now, const PanelData& data, bool fpsValid, float fps) {
    WatchData& w = m_lastWatch;
    w.style = m_config.watch.style;
    w.elements = m_config.watch.elements;
    w.theme = data.theme;
    w.english = m_config.english;
    const int seconds = LocalClockSeconds();
    w.hour = seconds / 3600;
    w.minute = (seconds / 60) % 60;
    w.second = seconds % 60;
    w.sessionSeconds = now - m_sessionStart;
    w.fpsValid = fpsValid;
    w.fps = fps;
    w.targetFps = data.targetFps;
    for (int hand = 0; hand < 2; ++hand) {
        float level = 0.f;
        bool charging = false;
        if ((w.elements & kWatchBattery) && m_vr.battery(hand, level, charging) && level >= 0.f && level <= 1.f) {
            w.battery[hand] = level;
            w.charging[hand] = charging;
        } else {
            w.battery[hand] = std::numeric_limits<float>::quiet_NaN();
            w.charging[hand] = false;
        }
    }
}

void VrHost::setShown(VrOverlay overlay, bool shown) {
    const int i = static_cast<int>(overlay);
    if (m_shown[i] == shown) return;
    m_shown[i] = shown;
    m_vr.show(overlay, shown);
}

void VrHost::hideAll() {
    if (!m_overlaysReady) return;
    setShown(VrOverlay::Panel, false);
    setShown(VrOverlay::Watch, false);
    setShown(VrOverlay::Window, false);
}

bool VrHost::placeOverlay(VrOverlay overlay, Anchor anchor, HandPlacement placement, float along, float lift, bool gazeOnly,
                          const Pose& viewPose, bool& looked, double now) {
    if (anchor == Anchor::View) return sendPlacement(overlay, 1, -1, viewPose, now);
    const int hand = anchor == Anchor::LeftHand ? 0 : 1;
    const Pose local = HandPlacementPose(hand == 0 ? Hand::Left : Hand::Right, placement, along, lift);
    // Manette absente ou non suivie : le calque disparaît plutôt que de rester figé n'importe où.
    Pose grip;
    if (!m_vr.gripInHead(hand, grip)) {
        looked = false;
        return false;
    }
    if (gazeOnly) {
        looked = IsLookedAt(Compose(grip, local), looked);
        if (!looked) return false;
    }
    return sendPlacement(overlay, 2, hand, local, now);
}

// Position envoyée à SteamVR seulement si elle change (ou toutes les 2 s) : sur la tête ou sur une manette,
// SteamVR suit lui-même l'appareil. `kind` : 1 tête, 2 main `hand`, 3 pièce.
bool VrHost::sendPlacement(VrOverlay overlay, int kind, int hand, const Pose& pose, double now) {
    Placement& last = m_placement[static_cast<int>(overlay)];
    const bool stale = last.sentAt < 0.0 || now - last.sentAt >= kPlacementRefreshSeconds;
    if (!stale && last.kind == kind && last.hand == hand && SamePose(last.pose, pose)) return true;
    if (kind == 1) {
        m_vr.placeOnHead(overlay, pose);
    } else if (kind == 2) {
        if (!m_vr.placeOnHand(overlay, hand, pose)) {
            last = Placement();
            return false;
        }
    } else {
        m_vr.placeInWorld(overlay, pose);
    }
    last.kind = kind;
    last.hand = hand;
    last.pose = pose;
    last.sentAt = now;
    return true;
}

// Fenêtre capturée : l'image est demandée à XRPerfSensors et relue quand elle change.
void VrHost::updateWindowImage(double now) {
    if (m_windowWantedAt < 0.0 || now - m_windowWantedAt >= 0.5) {
        m_windowShare.markWanted();
        m_windowWantedAt = now;
    }
    if (m_windowPending) return;  // la précédente n'est pas encore envoyée (fenêtre masquée)
    int width = 0, height = 0;
    if (m_windowShare.readNew(m_windowPixels, width, height)) {
        m_windowWidth = width;
        m_windowHeight = height;
        m_windowPending = true;
    }
}

bool VrHost::placeWindow(double now) {
    WindowConfig& w = m_config.window;
    if (m_windowWidth <= 0 || m_windowHeight <= 0) return false;  // pas encore d'image : Ctrl+Alt+G attend la première
    const float height = w.width * static_cast<float>(m_windowHeight) / static_cast<float>(m_windowWidth);
    if (w.anchor != WindowAnchor::World && !m_windowPlaceRequested) {
        const int hand = w.anchor == WindowAnchor::LeftHand ? 0 : 1;
        const Pose local = WindowHandPose(hand == 0 ? Hand::Left : Hand::Right, w.placement, w.along, w.lift, height);
        Pose grip;
        if (!m_vr.gripInHead(hand, grip)) {
            m_looked[2] = false;
            return false;
        }
        if (w.gazeOnly) {
            m_looked[2] = IsLookedAt(Compose(grip, local), m_looked[2]);
            if (!m_looked[2]) return false;
        }
        return sendPlacement(VrOverlay::Window, 2, hand, local, now);
    }
    // Pas encore posée (ou Ctrl+Alt+G) : devant soi, maintenant.
    if (!w.worldSet || m_windowPlaceRequested) {
        Pose head;
        if (!m_vr.headInWorld(head)) return false;  // tête non suivie : nouvel essai au passage suivant
        w.world = PlaceInFront(head, w.distance);
        w.worldSet = true;
        m_windowPlaceRequested = false;
        SaveState(m_config);  // enregistrée tout de suite, pour les jeux suivants
    }
    return sendPlacement(VrOverlay::Window, 3, -1, w.world, now);
}

// Image d'un calque : envoyée à SteamVR seulement quand il est visible (une montre « au regard » est
// le plus souvent masquée : rien n'est envoyé pendant ce temps).
void VrHost::upload(VrOverlay overlay) {
    const int i = static_cast<int>(overlay);
    if (overlay == VrOverlay::Panel) {
        m_panel.render(m_lastData, m_config.opacity, m_config.globalOpacity);
        const int height = Panel::contentHeight(m_lastData);
        m_vr.setPixels(overlay, m_panel.pixels(), Panel::kWidth, height);
        m_vr.setWidth(overlay, m_config.width);
    } else if (overlay == VrOverlay::Watch) {
        m_watch.render(m_lastWatch, m_config.watch.opacity);
        m_vr.setPixels(overlay, m_watch.pixels(), WatchFace::kSize, WatchFace::kSize);
        m_vr.setWidth(overlay, m_config.watch.size);
    } else {
        // Image de la capture, en BGRA.
        m_vr.setPixels(overlay, m_windowPixels.data(), m_windowWidth, m_windowHeight, true);
        m_windowPending = false;
    }
    m_hasPixels[i] = true;
    m_overlayDirty[i] = false;
}

void VrHost::display(double now) {
    if (!m_session.display || !m_overlaysReady) return;
    // Images des calques perdues côté SteamVR (textures recréées après une réinitialisation de la carte
    // graphique) : toutes à renvoyer, y compris celle de la fenêtre, qui peut ne plus changer.
    const uint32_t generation = m_vr.imageGeneration();
    if (generation != m_imageGeneration) {
        m_imageGeneration = generation;
        for (bool& has : m_hasPixels) has = false;
        if (!m_windowPixels.empty()) m_windowPending = true;
    }
    const bool wantPanel = m_config.visible, wantWatch = m_config.watch.enabled;
    if (m_dirty) {
        // Nouveau relevé (quelques fois par seconde) ou nouveaux réglages : à redessiner.
        m_overlayDirty[0] = m_overlayDirty[1] = true;
        m_dirty = false;
    }

    // Position et visibilité, à chaque passage : le regard vers le poignet change vite.
    bool panelShown = false, watchShown = false;
    if (wantPanel)
        panelShown = placeOverlay(VrOverlay::Panel, m_config.anchor, m_config.handPlacement, m_config.handAlong, m_config.handLift, m_config.gazeOnly,
                                  PanelViewPose(m_config), m_looked[0], now);
    if (wantWatch) {
        const WatchConfig& w = m_config.watch;
        watchShown = placeOverlay(VrOverlay::Watch, w.anchor, w.placement, w.along, w.lift, w.gazeOnly, WatchViewPose(), m_looked[1], now);
    }
    if (panelShown && (m_overlayDirty[0] || !m_hasPixels[0])) upload(VrOverlay::Panel);
    if (watchShown && (m_overlayDirty[1] || !m_hasPixels[1])) upload(VrOverlay::Watch);
    setShown(VrOverlay::Panel, panelShown);
    setShown(VrOverlay::Watch, watchShown);

    // Fenêtre capturée.
    bool windowShown = false;
    if (m_config.window.enabled) {
        updateWindowImage(now);
        windowShown = placeWindow(now);
        if (windowShown && m_windowPending) upload(VrOverlay::Window);
        windowShown = windowShown && m_hasPixels[2];
        if (windowShown && m_windowSentWidth != m_config.window.width) {
            m_vr.setWidth(VrOverlay::Window, m_config.window.width);
            m_windowSentWidth = m_config.window.width;
        }
    }
    setShown(VrOverlay::Window, windowShown);
}

void VrHost::openSummary() {
    std::string game = m_gameText;
    const size_t dot = game.find_last_of('.');
    if (dot != std::string::npos) game.erase(dot);
    // Nom de fichier : seulement des caractères sûrs (le nom vient d'un autre programme).
    for (char& c : game)
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    if (game.empty() || game == "?") game = "OpenVR";
    const std::wstring fileName = LocalTimeFileStamp() + L"_" + Widen(game) + L".txt";
    if (!m_summary.open(m_config.summaryFolder, fileName)) {
        const std::wstring fallback = FallbackSummaryFolder();
        if (fallback.empty() || !m_summary.open(fallback, fileName)) Log("XRPerfVR : impossible de creer le rapport de session.");
    }
}

void VrHost::writeSummary(double now, bool finished) {
    if (!m_summary.isOpen()) return;
    const std::string nowText = LocalTimeText();
    SessionInfo info;
    info.version = XRPO_VERSION;
    info.game = m_gameText.c_str();
    info.api = "non relevée (jeu OpenVR)";
    info.headset = m_headsetText.c_str();
    info.system = m_info.headset.c_str();
    info.runtime = m_info.runtime.c_str();
    info.openvr = true;
    info.recommendedWidth = m_info.recommendedWidth;
    info.recommendedHeight = m_info.recommendedHeight;
    if (m_config.summaryHardware) {
        info.cpu = m_cpuText.c_str();
        info.gpu = m_gpuText.c_str();
    }
    info.startText = m_recordStartText.c_str();
    info.nowText = nowText.c_str();
    info.finished = finished;
    info.wallSeconds = now - m_recordStart;
    info.english = m_config.english;
    info.manual = m_recordManual;
    const std::string text = m_sessionStats.format(info);
    if (!m_summary.kept()) {
        m_summary.keep();
        Log("XRPerfVR : rapport de session %s", Narrow(m_summary.path()).c_str());
    }
    m_summary.write(text.data(), text.size());
    m_lastSummaryWrite = now;
}

bool VrHost::finishSummary(double now) {
    if (!m_summary.isOpen()) return false;
    const bool keep = m_session.report && KeepReport(m_recordManual, m_sessionStats.activeSeconds(), now - m_recordStart);
    if (keep) writeSummary(now, true);
    m_summary.close();
    return keep;
}

void VrHost::startRecording(double now, bool manual) {
    m_sessionStats.restart();  // le rapport ne compte que la durée de l'enregistrement
    m_recording = true;
    m_recordManual = manual;
    m_recordStart = now;
    m_recordStartText = LocalTimeText();
    m_lastSummaryWrite = now;
    openSummary();
    if (manual) Log("XRPerfVR : enregistrement manuel demarre.");
}

void VrHost::stopRecording(double now) {
    if (!m_recording) return;
    const bool kept = finishSummary(now);
    if (m_recordManual) {
        m_reportControl.stoppedManual(now, kept, now - m_recordStart);
        Log(kept ? "XRPerfVR : enregistrement manuel arrete, rapport ecrit." : "XRPerfVR : enregistrement manuel arrete, trop court.");
    }
    m_recording = m_recordManual = false;
}

}  // namespace xrpo
