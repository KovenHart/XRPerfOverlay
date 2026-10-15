// Chronométrage d'une image entre xrWaitFrame, xrBeginFrame et xrEndFrame, et cadence demandée au jeu.
// Code portable (aucune dépendance Windows), testé dans tests/preview.cpp.
#pragma once

#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>

namespace xrpo {

class FrameTimer {
  public:
    void reset() {
        m_waiting.clear();
        m_begun = std::numeric_limits<double>::quiet_NaN();
        m_periodNs = 0;
    }

    // Au retour de xrWaitFrame. `periodNs` = durée d'affichage d'une image annoncée par le runtime.
    void onWaitFrame(double now, int64_t periodNs) {
        m_waiting.push_back(now);
        while (m_waiting.size() > 4) m_waiting.pop_front();
        // C'est la cadence que le jeu doit tenir à cet instant. Elle peut être une fraction de la
        // fréquence du casque : avec une génération d'images intermédiaires (framegen) ou une
        // reprojection bridée à 1/2, un casque à 72 Hz demande 36 images réelles par seconde.
        // Bornes de vraisemblance : entre 10 et 250 Hz.
        if (periodNs >= 4000000 && periodNs <= 100000000) m_periodNs = periodNs;
    }

    // Après tout xrBeginFrame réussi : l'image commencée est la plus ancienne en attente. Si la
    // précédente n'a jamais été soumise (image abandonnée ou vide), elle est simplement remplacée,
    // ce qui empêche tout décalage durable entre les appels.
    void onBeginFrame() {
        if (m_waiting.empty()) {
            m_begun = std::numeric_limits<double>::quiet_NaN();
            return;
        }
        m_begun = m_waiting.front();
        m_waiting.pop_front();
    }

    // À l'appel de xrEndFrame. Retourne le temps écoulé depuis le retour du xrWaitFrame de cette
    // image, en millisecondes, ou NaN s'il est inconnu ou invraisemblable.
    float onEndFrame(double now) {
        const double begun = m_begun;
        m_begun = std::numeric_limits<double>::quiet_NaN();
        if (std::isnan(begun)) return std::numeric_limits<float>::quiet_NaN();
        const double ms = (now - begun) * 1000.0;
        if (ms < 0.0 || ms > 1000.0) return std::numeric_limits<float>::quiet_NaN();
        return static_cast<float>(ms);
    }

    // Cadence demandée au jeu en images par seconde, 0 si inconnue.
    float targetFps() const { return m_periodNs > 0 ? static_cast<float>(1e9 / static_cast<double>(m_periodNs)) : 0.f; }

  private:
    std::deque<double> m_waiting;  // retours de xrWaitFrame pas encore suivis d'un xrBeginFrame
    double m_begun = std::numeric_limits<double>::quiet_NaN();
    int64_t m_periodNs = 0;
};

}  // namespace xrpo
