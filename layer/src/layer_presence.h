// Présence de la layer OpenXR dans un processus, pour XRPerfVR (le programme qui affiche l'overlay
// dans les jeux SteamVR / OpenVR). Un jeu OpenXR lancé à travers SteamVR est vu par SteamVR comme
// n'importe quel jeu : sans ce signal, il aurait deux panneaux et deux rapports.
//
// Deux objets nommés, propres au processus du jeu, existent tant que la session OpenXR est ouverte :
//   - « mesure » : la layer mesure la cadence et écrit le rapport de session ;
//   - « affichage » : la layer affiche elle-même le panneau et la montre (Direct3D 11 / 12). Absent
//     pour un jeu Vulkan ou OpenGL : XRPerfVR peut alors afficher l'overlay à sa place.
#pragma once

#include <cstdint>

namespace xrpo {

class LayerPresence {
  public:
    LayerPresence() = default;
    LayerPresence(const LayerPresence&) = delete;
    LayerPresence& operator=(const LayerPresence&) = delete;
    ~LayerPresence() { set(false, false); }

    void set(bool measuring, bool displaying);

  private:
    void* m_measuring = nullptr;
    void* m_displaying = nullptr;
};

// Côté XRPerfVR : la layer mesure / affiche-t-elle dans le processus `pid` ?
bool LayerMeasuresIn(uint32_t pid);
bool LayerDisplaysIn(uint32_t pid);

}  // namespace xrpo
