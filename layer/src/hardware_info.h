// Modèles du processeur et de la carte graphique, pour l'en-tête des rapports de session (option
// [summary] hardware). Lus une fois par session, à sa création ; rien de tout cela ne touche au
// rendu du jeu.
#pragma once

#include <string>

#include "xr.h"

namespace xrpo {

// Nom commercial du processeur (registre de Windows), ou chaîne vide.
std::string CpuModelName();

// Nom de la carte graphique d'identifiant `luid` (celle du périphérique Direct3D du jeu), ou vide.
std::string GpuNameFromLuid(LUID luid);

// Sans périphérique Direct3D (Vulkan, OpenGL) : la carte qui a le plus de mémoire vidéo, comme
// XRPerfSensors. Chaîne vide si aucune.
std::string MainGpuName();

}  // namespace xrpo
