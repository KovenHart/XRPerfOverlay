// Cumul des mesures sur toute une session de jeu et mise en forme du résumé texte.
// Code portable (aucune dépendance Windows), testé dans tests/preview.cpp.
//
// Le résumé est lu par la fenêtre "Rapports" de XRPerfSensors (sensors/ReportsWindow.cs) : les
// intitulés des lignes et la disposition des tableaux y sont reconnus. Toute modification doit y
// être reportée.
#pragma once

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "panel.h"

namespace xrpo {

// Renseignements fournis par l'appelant pour l'en-tête du résumé.
struct SessionInfo {
    const char* version = "";     // version de XR Perf Overlay (vide = non indiquée)
    const char* game = "?";       // nom de l'exécutable
    const char* api = "?";        // API graphique
    const char* headset = "";     // nom du casque affiché (vide = inconnu)
    const char* system = "";      // nom annoncé par le runtime, s'il diffère du nom affiché
    const char* runtime = "";     // runtime OpenXR et sa version (vide = inconnu)
    int recommendedWidth = 0;     // résolution par œil recommandée par le runtime (0 = inconnue)
    bool openvr = false;          // jeu SteamVR (OpenVR), mesuré par XRPerfVR : `runtime` est la version de SteamVR
    int recommendedHeight = 0;
    const char* cpu = nullptr;    // modèle du processeur (nul = ligne absente, option désactivée ; vide = inconnu)
    const char* gpu = nullptr;    // modèle de la carte graphique (idem)
    const char* startText = "?";  // date et heure de début
    const char* nowText = "?";    // date et heure du relevé
    bool finished = false;        // true : session terminée proprement
    double wallSeconds = 0.0;     // temps écoulé depuis le début de la session
    bool english = false;         // résumé en anglais (sinon en français) ; XRPerfSensors lit les deux
    bool manual = false;          // enregistrement démarré et arrêté au raccourci clavier (mode manuel)
};

class SessionStats {
  public:
    SessionStats() { reset(); }

    // Nouveau rapport en cours de session (enregistrement manuel, changement de mode) : tout repart de
    // zéro, sauf la mise à l'écart de la mesure GPU, qui vaut pour toute la session.
    void restart() {
        const bool rejected = m_gpuRejected;
        const float measured = m_rejectedMeasured, estimate = m_rejectedEstimate, load = m_rejectedLoad;
        reset();
        if (rejected) rejectGpuMeasure(measured, estimate, load);
    }

    void reset() {
        std::memset(m_histogram, 0, sizeof(m_histogram));
        m_lastFrame = -1.0;
        m_activeSeconds = 0.0;
        m_frames = 0;
        m_pauses = 0;
        m_worstMs = 0.f;
        m_cpuFrame = m_gpuFrame = m_fps = m_outFps = Accum();
        m_cpuLoad = m_cpuTemp = m_cpuMHz = m_gpuLoad = m_gpuTemp = m_gpuMHz = m_vramUsed = Accum();
        m_vramTotalMB = 0.f;
        m_samples = m_sensorSamples = m_targetSamples = m_targetHeld = 0;
        m_target = 0.f;
        m_render = m_renderMin = m_renderMax = Size();
        m_gpuMeasured = Accum();
        m_gpuRejected = false;
        m_rejectedMeasured = m_rejectedEstimate = m_rejectedLoad = 0.f;
        m_start = -1.0;
        m_minutes.clear();
        m_minutes.reserve(180);  // trois heures sans nouvelle allocation
    }

    // Durée cumulée des images mesurées (les coupures de plus d'une seconde ne comptent pas).
    double activeSeconds() const { return m_activeSeconds; }

    // À chaque image soumise. `cpuMs` peut valoir NaN.
    void onFrame(double now, float cpuMs) {
        if (m_lastFrame >= 0.0) {
            const double dt = now - m_lastFrame;
            if (dt > 0.0 && dt <= kPauseSeconds) {
                const float ms = static_cast<float>(dt * 1000.0);
                m_activeSeconds += dt;
                ++m_frames;
                ++m_histogram[bucketOf(ms)];
                if (ms > m_worstMs) m_worstMs = ms;
                m_cpuFrame.add(cpuMs);
                if (Minute* m = minuteAt(now)) {
                    m->activeSeconds += dt;
                    ++m->frames;
                    m->cpuMs.add(cpuMs);
                }
            } else if (dt > kPauseSeconds) {
                ++m_pauses;  // chargement, pause, casque retiré
            }
        }
        m_lastFrame = now;
    }

    // Temps GPU mesuré d'une image (option de mesure réelle), relevé à l'instant `now` de sa fin.
    void onGpuFrame(double now, float gpuMs) {
        if (std::isnan(gpuMs) || m_gpuRejected) return;
        m_gpuMeasured.add(gpuMs);
        if (Minute* m = minuteAt(now)) m->gpuMeasured.add(gpuMs);
    }

    // Mesure du temps GPU jugée non représentative (voir Overlay::checkGpuPlausibility) : les mesures
    // déjà cumulées sont oubliées, le rapport ne donne plus que l'estimation et explique pourquoi.
    void rejectGpuMeasure(float measuredMs, float estimateMs, float gpuLoad) {
        m_gpuRejected = true;
        m_rejectedMeasured = measuredMs;
        m_rejectedEstimate = estimateMs;
        m_rejectedLoad = gpuLoad;
        m_gpuMeasured = Accum();
        for (Minute& minute : m_minutes) minute.gpuMeasured = Accum();
    }

    // À chaque relevé (quelques fois par seconde). `fullSecond` : le FPS affiché couvre une seconde pleine.
    void onSample(const PanelData& d, bool fullSecond, double now) {
        ++m_samples;
        Minute* minute = minuteAt(now);
        if (d.renderWidth > 0 && d.renderHeight > 0) {
            // Dernière résolution vue, et extrêmes si le jeu en change en cours de route.
            const Size size = {d.renderWidth, d.renderHeight};
            if (m_render.width == 0 || size.pixels() < m_renderMin.pixels()) m_renderMin = size;
            if (m_render.width == 0 || size.pixels() > m_renderMax.pixels()) m_renderMax = size;
            m_render = size;
        }
        if (d.fpsValid && fullSecond) {
            m_fps.add(d.fps);
            m_outFps.add(d.outFps);
            if (minute) minute->fps.add(d.fps);
            if (d.targetFps > 0.f) {
                m_target = d.targetFps;
                ++m_targetSamples;
                if (d.fps >= 0.97f * d.targetFps) ++m_targetHeld;
            }
        }
        if (!d.sensorsOk) return;
        ++m_sensorSamples;
        m_cpuLoad.add(d.cpuLoad);
        m_cpuTemp.add(d.cpuTemp);
        m_cpuMHz.add(d.cpuMHz);
        m_gpuLoad.add(d.gpuLoad);
        m_gpuTemp.add(d.gpuTemp);
        m_gpuMHz.add(d.gpuMHz);
        m_vramUsed.add(d.vramUsedMB);
        m_gpuFrame.add(d.gpuFrameEstimateMs);
        if (!std::isnan(d.vramTotalMB) && d.vramTotalMB > m_vramTotalMB) m_vramTotalMB = d.vramTotalMB;
        if (minute) {
            minute->gpuEstimate.add(d.gpuFrameEstimateMs);
            minute->cpuLoad.add(d.cpuLoad);
            minute->gpuLoad.add(d.gpuLoad);
            minute->cpuTemp.add(d.cpuTemp);
            minute->gpuTemp.add(d.gpuTemp);
            minute->vram.add(d.vramUsedMB);
        }
    }

    // Part des images (0 à 1) dont la durée est au plus `maxMs` (bornes de l'histogramme : 0,1 ms puis 5 ms).
    double shareAtMost(float maxMs) const {
        if (m_frames == 0) return std::nan("");
        uint64_t count = 0;
        for (int i = 0; i < kBuckets && bucketCenter(i) <= maxMs; ++i) count += m_histogram[i];
        return static_cast<double>(count) / static_cast<double>(m_frames);
    }

    // Temps d'image (ms) dépassé par la fraction `slowFraction` des images les plus lentes.
    // Retourne NaN s'il n'y a pas assez d'images pour que la valeur ait un sens.
    float slowFrameMs(double slowFraction) const {
        if (static_cast<double>(m_frames) * slowFraction < 10.0) return std::nanf("");
        const uint64_t wanted = static_cast<uint64_t>(std::ceil(static_cast<double>(m_frames) * slowFraction));
        uint64_t seen = 0;
        for (int i = kBuckets - 1; i >= 0; --i) {
            seen += m_histogram[i];
            if (seen >= wanted) return bucketCenter(i);
        }
        return std::nanf("");
    }

    // Le résumé complet, en UTF-8 avec marque d'ordre d'octets et fins de ligne Windows. En français ou
    // en anglais : sensors/SessionReport.cs reconnaît les intitulés des deux langues.
    std::string format(const SessionInfo& info) const {
        Writer w;
        char a[32], b[32], c[32];
        const bool en = info.english;
        const auto T = [en](const char* french, const char* english) { return en ? english : french; };
        w.put("\xEF\xBB\xBF");
        if (info.version[0])
            w.put(T("XR Perf Overlay %s - résumé de session\r\n\r\n", "XR Perf Overlay %s - session summary\r\n\r\n"), info.version);
        else
            w.put(T("XR Perf Overlay - résumé de session\r\n\r\n", "XR Perf Overlay - session summary\r\n\r\n"));
        w.put(T("Jeu              : %s\r\n", "Game             : %s\r\n"), info.game);
        w.put(T("API graphique    : %s\r\n", "Graphics API     : %s\r\n"), info.api);
        if (info.system[0] && std::strcmp(info.system, info.headset) != 0)
            w.put(T("Casque           : %s   (nom annoncé par le runtime : %s)\r\n", "Headset          : %s   (name reported by the runtime: %s)\r\n"),
                  info.headset[0] ? info.headset : "--", info.system);
        else
            w.put(T("Casque           : %s\r\n", "Headset          : %s\r\n"), info.headset[0] ? info.headset : "--");
        if (info.openvr)
            w.put(T("Runtime OpenVR   : %s\r\n", "OpenVR runtime   : %s\r\n"), info.runtime[0] ? info.runtime : "--");
        else
            w.put(T("Runtime OpenXR   : %s\r\n", "OpenXR runtime   : %s\r\n"), info.runtime[0] ? info.runtime : "--");
        if (info.cpu) w.put(T("Processeur       : %s\r\n", "Processor        : %s\r\n"), info.cpu[0] ? info.cpu : "--");
        if (info.gpu) w.put(T("Carte graphique  : %s\r\n", "Graphics card    : %s\r\n"), info.gpu[0] ? info.gpu : "--");
        if (m_render.width == 0)
            w.put(T("Rendu par œil    : --\r\n", "Render per eye   : --\r\n"));
        else if (m_renderMin.pixels() == m_renderMax.pixels())
            w.put(T("Rendu par œil    : %d x %d\r\n", "Render per eye   : %d x %d\r\n"), m_render.width, m_render.height);
        else
            w.put(T("Rendu par œil    : %d x %d   (a varié de %d x %d à %d x %d)\r\n", "Render per eye   : %d x %d   (varied from %d x %d to %d x %d)\r\n"),
                  m_render.width, m_render.height, m_renderMin.width, m_renderMin.height, m_renderMax.width, m_renderMax.height);
        if (info.recommendedWidth > 0 && info.recommendedHeight > 0) {
            if (m_render.width > 0)
                w.put(T("Rendu recommandé : %d x %d   (par le runtime ; le jeu rend à %.0f %% en largeur)\r\n",
                        "Recommended render : %d x %d   (by the runtime; the game renders at %.0f %% of the width)\r\n"),
                      info.recommendedWidth, info.recommendedHeight, 100.0 * m_render.width / info.recommendedWidth);
            else
                w.put(T("Rendu recommandé : %d x %d   (par le runtime)\r\n", "Recommended render : %d x %d   (by the runtime)\r\n"),
                      info.recommendedWidth, info.recommendedHeight);
        }
        w.put(T("Début            : %s\r\n", "Start            : %s\r\n"), info.startText);
        if (info.finished)
            w.put(T("Fin              : %s\r\n", "End              : %s\r\n"), info.nowText);
        else
            w.put(T("Dernier relevé   : %s  (session en cours, ou jeu fermé sans terminer sa session)\r\n",
                    "Last update      : %s  (session in progress, or game closed without ending its session)\r\n"), info.nowText);
        if (info.manual)
            w.put(T("Enregistrement   : manuel (raccourci clavier)\r\n", "Recording        : manual (hotkey)\r\n"));
        w.put(T("Durée            : %s, dont %s d'images mesurées\r\n", "Duration         : %s, of which %s of measured frames\r\n"),
              duration(a, info.wallSeconds), duration(b, m_activeSeconds));

        const float avgFps = m_activeSeconds > 0.0 ? static_cast<float>(static_cast<double>(m_frames) / m_activeSeconds) : std::nanf("");
        const float low1 = slowFrameMs(0.01), low01 = slowFrameMs(0.001);
        w.put(T("\r\nIMAGES PAR SECONDE\r\n", "\r\nFRAMES PER SECOND\r\n"));
        w.put(T("  Moyenne        : %s\r\n", "  Average        : %s\r\n"), num(a, avgFps, 1));
        w.put(T("  Minimum        : %s   (cadence la plus basse sur une seconde)\r\n", "  Minimum        : %s   (lowest rate over one second)\r\n"), num(a, m_fps.min(), 0));
        w.put(T("  Maximum        : %s   (cadence la plus haute sur une seconde)\r\n", "  Maximum        : %s   (highest rate over one second)\r\n"), num(a, m_fps.max(), 0));
        w.put(T("  1 %% low        : %s   (99 %% des images ont été plus rapides)\r\n", "  1 %% low        : %s   (99 %% of frames were faster)\r\n"), num(a, 1000.f / low1, 1));
        w.put(T("  0,1 %% low      : %s   (99,9 %% des images ont été plus rapides)\r\n", "  0.1 %% low      : %s   (99.9 %% of frames were faster)\r\n"), num(a, 1000.f / low01, 1));
        if (m_targetSamples > 0)
            w.put(T("  Cible          : %.0f, tenue %.0f %% du temps\r\n", "  Target         : %.0f, held %.0f %% of the time\r\n"), static_cast<double>(m_target),
                  100.0 * static_cast<double>(m_targetHeld) / static_cast<double>(m_targetSamples));
        else
            w.put(T("  Cible          : --\r\n", "  Target         : --\r\n"));
        if (m_outFps.n > 0)
            w.put(T("  Avec framegen  : %s   (images envoyées au casque, générées comprises ; min %s, max %s)\r\n",
                    "  With framegen  : %s   (frames sent to the headset, generated ones included; min %s, max %s)\r\n"),
                  num(a, m_outFps.avg(), 1), num(b, m_outFps.min(), 0), num(c, m_outFps.max(), 0));
        w.put(T("  Images rendues : %.0f\r\n", "  Frames rendered : %.0f\r\n"), static_cast<double>(m_frames));
        w.put(T("  Coupures       : %u   (chargements et pauses de plus d'une seconde, non comptés)\r\n",
                "  Interruptions  : %u   (loading and pauses longer than one second, not counted)\r\n"), m_pauses);

        w.put(T("\r\nTEMPS PAR IMAGE (ms)       moyenne       max\r\n", "\r\nFRAME TIME (ms)            average       max\r\n"));
        w.put(T("  Image complète         %8s  %8s   (intervalle entre deux images)\r\n", "  Whole frame            %8s  %8s   (interval between two frames)\r\n"),
              num(a, 1000.f / avgFps, 1), num(b, m_frames > 0 ? m_worstMs : std::nanf(""), 1));
        w.put("  CPU                    %8s  %8s\r\n", num(a, m_cpuFrame.avg(), 1), num(b, m_cpuFrame.max(), 1));
        if (m_gpuMeasured.n > 0)
            w.put(T("  GPU (mesuré)           %8s  %8s\r\n", "  GPU (measured)         %8s  %8s\r\n"), num(a, m_gpuMeasured.avg(), 1), num(b, m_gpuMeasured.max(), 1));
        w.put(T("  GPU (estimé)           %8s  %8s\r\n", "  GPU (estimated)        %8s  %8s\r\n"), num(a, m_gpuFrame.avg(), 1), num(b, m_gpuFrame.max(), 1));

        w.put(T("\r\nPROCESSEUR                 moyenne       max\r\n", "\r\nPROCESSOR                  average       max\r\n"));
        w.put(T("  Charge (%%)             %8s  %8s\r\n", "  Load (%%)               %8s  %8s\r\n"), num(a, m_cpuLoad.avg(), 0), num(b, m_cpuLoad.max(), 0));
        w.put(T("  Température (°C)       %8s  %8s\r\n", "  Temperature (°C)       %8s  %8s\r\n"), num(a, m_cpuTemp.avg(), 0), num(b, m_cpuTemp.max(), 0));
        w.put(T("  Fréquence (MHz)        %8s  %8s\r\n", "  Clock (MHz)            %8s  %8s\r\n"), num(a, m_cpuMHz.avg(), 0), num(b, m_cpuMHz.max(), 0));

        w.put(T("\r\nCARTE GRAPHIQUE            moyenne       max\r\n", "\r\nGRAPHICS CARD              average       max\r\n"));
        w.put(T("  Charge (%%)             %8s  %8s\r\n", "  Load (%%)               %8s  %8s\r\n"), num(a, m_gpuLoad.avg(), 0), num(b, m_gpuLoad.max(), 0));
        w.put(T("  Température (°C)       %8s  %8s\r\n", "  Temperature (°C)       %8s  %8s\r\n"), num(a, m_gpuTemp.avg(), 0), num(b, m_gpuTemp.max(), 0));
        w.put(T("  Fréquence (MHz)        %8s  %8s\r\n", "  Clock (MHz)            %8s  %8s\r\n"), num(a, m_gpuMHz.avg(), 0), num(b, m_gpuMHz.max(), 0));
        w.put(T("  VRAM (Go)              %8s  %8s   sur %s\r\n", "  VRAM (GB)              %8s  %8s   of %s\r\n"), num(a, m_vramUsed.avg() / 1024.f, 1),
              num(b, m_vramUsed.max() / 1024.f, 1), num(c, m_vramTotalMB > 0.f ? m_vramTotalMB / 1024.f : std::nanf(""), 1));

        // Répartition des temps d'image par rapport au budget d'une image (durée à ne pas dépasser pour tenir la cible).
        if (m_target > 0.f && m_frames > 0) {
            const float budget = 1000.f / m_target;
            const double held = shareAtMost(budget * 1.05f), mid = shareAtMost(budget * 1.5f), twice = shareAtMost(budget * 2.f);
            w.put(T("\r\nRÉPARTITION DES TEMPS D'IMAGE   (budget d'une image pour tenir %.0f : %.1f ms)\r\n",
                    "\r\nFRAME TIME DISTRIBUTION   (frame budget to hold %.0f : %.1f ms)\r\n"), static_cast<double>(m_target), static_cast<double>(budget));
            w.put(T("  Dans le budget          : %5.1f %%\r\n", "  Within budget           : %5.1f %%\r\n"), 100.0 * held);
            w.put(T("  Jusqu'à 1,5 x le budget : %5.1f %%\r\n", "  Up to 1.5 x the budget  : %5.1f %%\r\n"), 100.0 * (mid - held));
            w.put(T("  De 1,5 à 2 x le budget  : %5.1f %%\r\n", "  1.5 to 2 x the budget   : %5.1f %%\r\n"), 100.0 * (twice - mid));
            w.put(T("  Plus de 2 x le budget   : %5.1f %%\r\n", "  Over 2 x the budget     : %5.1f %%\r\n"), 100.0 * (1.0 - twice));
        }

        // Relevé minute par minute (minutes comptées depuis le début de la session).
        if (!m_minutes.empty()) {
            // Avec la mesure réelle, une minute sans mesure (pilote muet un moment) reprend l'estimation,
            // précédée de "~".
            const bool measured = m_gpuMeasured.n > 0;
            bool mixed = false;
            if (measured)
                for (const Minute& r : m_minutes) mixed = mixed || (r.gpuMeasured.n == 0 && r.gpuEstimate.n > 0);
            w.put(T("\r\nRELEVÉ MINUTE PAR MINUTE   (GPU ms : %s)\r\n", "\r\nMINUTE BY MINUTE   (GPU ms : %s)\r\n"),
                  !measured ? T("estimé", "estimated")
                  : mixed   ? T("mesuré ; ~ = estimé, minute sans mesure", "measured; ~ = estimated, minute without measurement")
                            : T("mesuré", "measured"));
            w.put(T("  minute  FPS moy  FPS min  CPU ms  GPU ms  CPU %%  GPU %%  CPU °C  GPU °C  VRAM Go\r\n",
                    "  minute  FPS avg  FPS min  CPU ms  GPU ms  CPU %%  GPU %%  CPU °C  GPU °C  VRAM GB\r\n"));
            char e[32], f[32], g[32], h[32], k[32], l[32];
            for (size_t i = 0; i < m_minutes.size(); ++i) {
                const Minute& r = m_minutes[i];
                const float fps = r.activeSeconds > 0.5 ? static_cast<float>(static_cast<double>(r.frames) / r.activeSeconds) : std::nanf("");
                if (measured && r.gpuMeasured.n == 0 && r.gpuEstimate.n > 0)
                    std::snprintf(e, sizeof(e), "~%.1f", static_cast<double>(r.gpuEstimate.avg()));
                else
                    num(e, measured ? r.gpuMeasured.avg() : r.gpuEstimate.avg(), 1);
                w.put("  %6u  %7s  %7s  %6s  %6s  %5s  %5s  %6s  %6s  %7s\r\n", static_cast<unsigned>(i + 1), num(a, fps, 1), num(b, r.fps.min(), 0),
                      num(c, r.cpuMs.avg(), 1), e, num(f, r.cpuLoad.avg(), 0), num(g, r.gpuLoad.avg(), 0), num(h, r.cpuTemp.avg(), 0),
                      num(k, r.gpuTemp.avg(), 0), num(l, r.vram.avg() / 1024.f, 1));
            }
        }

        w.put("\r\n");
        if (m_sensorSamples == 0)
            w.put(T("Capteurs indisponibles : XRPerfSensors n'était pas lancé pendant cette session.\r\n",
                    "Sensors unavailable : XRPerfSensors was not running during this session.\r\n"));
        else if (m_sensorSamples < m_samples)
            w.put(T("Capteurs relevés pendant %.0f %% de la session seulement.\r\n", "Sensors read during only %.0f %% of the session.\r\n"),
                  100.0 * static_cast<double>(m_sensorSamples) / static_cast<double>(m_samples));
        if (m_gpuMeasured.n > 0)
            w.put(T("Le temps GPU mesuré est l'écart, sur le GPU, entre le début et la fin de chaque image du jeu (horodatages) : "
                    "quand le processeur est la limite, il compte aussi les moments où le GPU attend le travail suivant.\r\n",
                    "The measured GPU time is the time, on the GPU, between the start and the end of each game frame (timestamps): "
                    "when the processor is the bottleneck, it also counts the moments the GPU waits for the next work.\r\n"));
        if (m_gpuRejected)
            w.put(T("Mesure réelle du temps GPU écartée : %.1f ms mesuré contre %.1f ms estimé, carte chargée à %.0f %%. "
                    "Avec ce jeu, les repères de la layer n'encadrent pas son rendu ; le temps GPU de ce rapport est estimé.\r\n",
                    "Real GPU time measurement discarded : %.1f ms measured against %.1f ms estimated, card at %.0f %% load. "
                    "With this game, the layer's markers do not enclose its rendering; the GPU time in this report is estimated.\r\n"),
                  static_cast<double>(m_rejectedMeasured), static_cast<double>(m_rejectedEstimate), static_cast<double>(m_rejectedLoad));
        w.put(T("Le temps GPU estimé vaut charge GPU x durée d'une image : ce n'est pas une mesure image par image.\r\n",
                "The estimated GPU time is GPU load x frame duration: it is not a frame-by-frame measurement.\r\n"));
        return w.text();
    }

  private:
    static constexpr double kPauseSeconds = 1.0;
    // Histogramme des temps d'image : pas de 0,1 ms jusqu'à 100 ms, puis de 5 ms jusqu'à 1 s.
    static constexpr int kFineBuckets = 1000;
    static constexpr int kBuckets = kFineBuckets + 180;

    static int bucketOf(float ms) {
        if (ms < 100.f) return ms < 0.f ? 0 : static_cast<int>(ms * 10.f);
        const int i = kFineBuckets + static_cast<int>((ms - 100.f) / 5.f);
        return i < kBuckets ? i : kBuckets - 1;
    }
    static float bucketCenter(int i) {
        if (i < kFineBuckets) return (static_cast<float>(i) + 0.5f) / 10.f;
        return 100.f + (static_cast<float>(i - kFineBuckets) + 0.5f) * 5.f;
    }

    struct Size {
        int width = 0, height = 0;
        int64_t pixels() const { return static_cast<int64_t>(width) * height; }
    };

    struct Accum {
        double sum = 0.0;
        float lo = 0.f, hi = 0.f;
        uint64_t n = 0;
        void add(float v) {
            if (std::isnan(v)) return;
            if (n == 0 || v < lo) lo = v;
            if (n == 0 || v > hi) hi = v;
            sum += v;
            ++n;
        }
        float avg() const { return n ? static_cast<float>(sum / static_cast<double>(n)) : std::nanf(""); }
        float min() const { return n ? lo : std::nanf(""); }
        float max() const { return n ? hi : std::nanf(""); }
    };

    // Ajout de lignes mises en forme (chaque ligne tient largement en 2048 octets ; au-delà, coupée).
    class Writer {
      public:
        Writer() { m_text.reserve(8192); }
        void put(const char* format, ...) {
            char line[2048];
            va_list args;
            va_start(args, format);
            const int n = std::vsnprintf(line, sizeof(line), format, args);
            va_end(args);
            if (n <= 0) return;
            m_text.append(line, static_cast<size_t>(n) < sizeof(line) ? static_cast<size_t>(n) : sizeof(line) - 1);
        }
        const std::string& text() const { return m_text; }

      private:
        std::string m_text;
    };

    // Valeurs d'une minute de session.
    struct Minute {
        double activeSeconds = 0.0;
        uint32_t frames = 0;
        Accum fps, cpuMs, gpuMeasured, gpuEstimate, cpuLoad, gpuLoad, cpuTemp, gpuTemp, vram;
    };
    static constexpr size_t kMaxMinutes = 24 * 60;

    // Minute de la session qui contient l'instant `now` (créée au besoin), ou nul au-delà de 24 h.
    Minute* minuteAt(double now) {
        if (m_start < 0.0) m_start = now;
        const double elapsed = now - m_start;
        if (elapsed < 0.0) return nullptr;
        const size_t index = static_cast<size_t>(elapsed / 60.0);
        if (index >= kMaxMinutes) return nullptr;
        if (index >= m_minutes.size()) m_minutes.resize(index + 1);
        return &m_minutes[index];
    }

    // Nombre avec `decimals` décimales, ou "--" si la valeur est absente.
    static const char* num(char (&buf)[32], float value, int decimals) {
        if (std::isnan(value) || std::isinf(value))
            std::snprintf(buf, sizeof(buf), "--");
        else
            std::snprintf(buf, sizeof(buf), "%.*f", decimals, static_cast<double>(value));
        return buf;
    }

    static const char* duration(char (&buf)[32], double seconds) {
        const long total = static_cast<long>(seconds < 0.0 ? 0.0 : seconds + 0.5);
        const long h = total / 3600, m = (total % 3600) / 60, s = total % 60;
        if (h > 0)
            std::snprintf(buf, sizeof(buf), "%ld h %02ld min %02ld s", h, m, s);
        else if (m > 0)
            std::snprintf(buf, sizeof(buf), "%ld min %02ld s", m, s);
        else
            std::snprintf(buf, sizeof(buf), "%ld s", s);
        return buf;
    }

    uint32_t m_histogram[kBuckets];
    double m_lastFrame;
    double m_activeSeconds;
    uint64_t m_frames;
    unsigned m_pauses;
    float m_worstMs;
    Accum m_cpuFrame, m_gpuFrame, m_fps, m_outFps;
    Accum m_cpuLoad, m_cpuTemp, m_cpuMHz, m_gpuLoad, m_gpuTemp, m_gpuMHz, m_vramUsed;
    float m_vramTotalMB;
    uint64_t m_samples, m_sensorSamples, m_targetSamples, m_targetHeld;
    float m_target;
    Size m_render, m_renderMin, m_renderMax;  // image soumise par le jeu pour un œil
    Accum m_gpuMeasured;                      // temps GPU mesuré, image par image (option)
    bool m_gpuRejected;                       // mesure écartée en cours de session (non représentative)
    float m_rejectedMeasured, m_rejectedEstimate, m_rejectedLoad;
    double m_start;                           // instant du premier relevé (début des minutes)
    std::vector<Minute> m_minutes;
};

}  // namespace xrpo
