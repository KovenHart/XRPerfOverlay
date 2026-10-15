// Publication de l'image du panneau dans une mémoire partagée, pour la fenêtre du bureau affichée
// par XRPerfSensors.exe (à capturer dans OBS à côté du miroir du jeu).
#pragma once

#include <cstdint>

namespace xrpo {

class PanelShare {
  public:
    PanelShare() = default;
    PanelShare(const PanelShare&) = delete;
    PanelShare& operator=(const PanelShare&) = delete;
    ~PanelShare();

    // true si XRPerfSensors affiche sa fenêtre en ce moment. Sinon la layer ne fait aucun travail.
    bool wanted();

    // Copie Panel::kWidth x Panel::kHeight pixels RGBA (alpha prémultiplié). `contentHeight` : nombre
    // de lignes réellement dessinées en partant du haut, que la fenêtre doit montrer.
    void publish(const uint8_t* pixels, int contentHeight);

  private:
    void tryOpen();

    void* m_mapping = nullptr;
    uint8_t* m_view = nullptr;
    unsigned long long m_lastAttemptMs = 0;
};

}  // namespace xrpo
