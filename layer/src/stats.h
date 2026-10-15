// Statistiques de cadence : FPS instantané, moyenne / min / max sur une fenêtre glissante,
// et temps CPU par image. Code portable (aucune dépendance Windows) pour pouvoir être testé seul.
#pragma once

#include <cmath>
#include <deque>
#include <limits>

namespace xrpo {

struct StatsSnapshot {
    bool valid = false;      // false tant qu'il n'y a pas assez d'images récentes
    bool fullSecond = false; // `fps` couvre bien une seconde entière (pas un début de mesure)
    float fps = 0.f;         // moyenne sur la dernière seconde
    float avg = 0.f;         // moyenne sur la fenêtre
    float min = 0.f;         // plus petite / plus grande valeur du FPS "1 seconde" vue dans la fenêtre
    float max = 0.f;
    float cpuMs = std::numeric_limits<float>::quiet_NaN();  // temps CPU moyen par image (0,5 s)
};

class FrameStats {
  public:
    // Durée de la fenêtre AVG/MIN/MAX en secondes (bornée à [2 s, 600 s]).
    void setWindow(double seconds) {
        if (seconds < 2.0) seconds = 2.0;
        if (seconds > 600.0) seconds = 600.0;
        m_window = seconds;
    }

    void reset() {
        m_frames.clear();
        m_samples.clear();
    }

    // À appeler une fois par image soumise. `now` en secondes (horloge monotone),
    // `cpuMs` = temps CPU de l'image, ou NaN s'il est inconnu.
    void onFrame(double now, float cpuMs) {
        // Une coupure de plus d'une seconde (chargement, pause, casque retiré) n'est pas une
        // chute de performances : on repart de zéro plutôt que de fausser MIN et AVG.
        if (!m_frames.empty() && now - m_frames.back().t > kPauseSeconds) reset();
        m_frames.push_back({now, cpuMs});
        prune(now);
    }

    // À appeler à chaque rafraîchissement de l'affichage.
    StatsSnapshot snapshot(double now) {
        StatsSnapshot s;
        prune(now);
        if (m_frames.size() < 2) return s;
        const double last = m_frames.back().t;
        if (now - last > kPauseSeconds) return s;  // le jeu ne soumet plus d'images

        // FPS sur la dernière seconde.
        size_t count = 0;
        double first = last;
        for (auto it = m_frames.rbegin(); it != m_frames.rend() && last - it->t <= 1.0; ++it) {
            first = it->t;
            ++count;
        }
        const double span = last - first;
        if (count < 2 || span < 0.25) return s;
        s.valid = true;
        s.fps = static_cast<float>(static_cast<double>(count - 1) / span);

        // Moyenne sur toute la fenêtre.
        const double total = last - m_frames.front().t;
        s.avg = total > 0.0 ? static_cast<float>(static_cast<double>(m_frames.size() - 1) / total) : s.fps;

        // MIN / MAX : on n'enregistre un échantillon que lorsqu'une seconde pleine est disponible,
        // sinon les premières images après un chargement donneraient des extrêmes sans signification.
        s.fullSecond = span >= 0.9;
        if (s.fullSecond) m_samples.push_back({now, s.fps});
        s.min = s.max = s.fps;
        for (const Sample& smp : m_samples) {
            if (smp.fps < s.min) s.min = smp.fps;
            if (smp.fps > s.max) s.max = smp.fps;
        }

        // Temps CPU moyen sur la dernière demi-seconde.
        double sum = 0.0;
        size_t n = 0;
        for (auto it = m_frames.rbegin(); it != m_frames.rend() && last - it->t <= 0.5; ++it) {
            if (!std::isnan(it->cpuMs)) {
                sum += it->cpuMs;
                ++n;
            }
        }
        if (n > 0) s.cpuMs = static_cast<float>(sum / static_cast<double>(n));
        return s;
    }

  private:
    struct Frame {
        double t;
        float cpuMs;
    };
    struct Sample {
        double t;
        float fps;
    };

    static constexpr double kPauseSeconds = 1.0;

    void prune(double now) {
        while (!m_frames.empty() && now - m_frames.front().t > m_window) m_frames.pop_front();
        while (!m_samples.empty() && now - m_samples.front().t > m_window) m_samples.pop_front();
    }

    double m_window = 30.0;
    std::deque<Frame> m_frames;
    std::deque<Sample> m_samples;
};

}  // namespace xrpo
