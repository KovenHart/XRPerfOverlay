// Bloc de commandes de l'écran tactile (shared_block.h, section 6) : ce que XRPerfSensors doit faire
// (vidéo, direct, capture d'écran, preset d'image), et ce qu'il publie en retour pour l'affichage.
#pragma once

#include <cstdint>

#include "shared_block.h"
#include "touch_pad.h"

namespace xrpo {

class CommandShare {
  public:
    CommandShare() = default;
    CommandShare(const CommandShare&) = delete;
    CommandShare& operator=(const CommandShare&) = delete;
    ~CommandShare();

    struct State {
        bool online = false;  // XRPerfSensors tourne et lit les commandes
        uint32_t flags = 0;   // kCommandVideoReady | kCommandLiveReady | kCommandWindowReady
        char preset[32] = "";
    };
    // Dernier état publié par XRPerfSensors (online = false s'il ne répond pas).
    State read();
    // Dépose une commande ; false si XRPerfSensors ne répond pas.
    bool post(TouchAction action);

  private:
    bool open();

    void* m_mapping = nullptr;
    CommandBlock* m_view = nullptr;
    unsigned long long m_lastAttemptMs = 0;
};

}  // namespace xrpo
