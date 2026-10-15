// Aperçu du panneau hors jeu, pour la fenêtre du bureau de XRPerfSensors.exe : la même image que
// dans le casque, dessinée par le même code, avec des FPS d'exemple.
#pragma once

#include <cstdint>

#include "panel.h"
#include "shared_block.h"

namespace xrpo {

// Valeurs d'exemple (FPS, temps d'image, courbe) pour l'instant `seconds`. Les capteurs ne sont pas
// remplis ici. Déterministe : le même instant donne la même image.
void FillPreviewData(PanelData& data, double seconds);

}  // namespace xrpo

extern "C" {

// Fonction exportée par la DLL, appelée par XRPerfSensors.exe (voir sensors/PanelWindow.cs).
//   settings     : réglages à illustrer (obligatoire)
//   sensors      : dernières valeurs des capteurs, ou nul si elles ne sont pas disponibles
//   seconds      : temps écoulé, pour faire défiler la courbe
//   clockMinutes : heure locale en minutes depuis minuit
//   headsetName  : nom de casque à afficher (ASCII), ou nul
//   pixels       : tampon de sortie, Panel::kWidth x Panel::kHeight pixels RGBA (alpha prémultiplié)
//   capacity     : taille du tampon en octets
// Retourne la hauteur dessinée en pixels, ou 0 si un argument est invalide (rien n'est écrit).
int xrpoRenderPreview(const xrpo::SettingsBlock* settings, const xrpo::SensorBlock* sensors, double seconds, int clockMinutes,
                      const char* headsetName, uint8_t* pixels, uint32_t capacity);

// Aperçu de la montre (fenêtre de réglages de XRPerfSensors) : style, éléments, couleurs et opacité
// des réglages, valeurs d'exemple.
//   clockSeconds : heure locale en secondes depuis minuit
//   pixels       : tampon de sortie, WatchFace::kSize x WatchFace::kSize pixels RGBA (alpha prémultiplié)
// Retourne le côté de l'image en pixels, ou 0 si un argument est invalide (rien n'est écrit).
int xrpoRenderWatchPreview(const xrpo::SettingsBlock* settings, int clockSeconds, uint8_t* pixels, uint32_t capacity);
}
