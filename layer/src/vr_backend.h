// Ce dont XRPerfVR a besoin de SteamVR (OpenVR) et de Windows, derrière une interface : le vrai
// programme passe par openvr_backend.cpp, le banc d'essai par un faux SteamVR.
#pragma once

#include <cstdint>
#include <limits>
#include <string>

#include "pose_math.h"

namespace xrpo {

// Une image rendue par le jeu, d'après les relevés du compositeur de SteamVR.
struct VrFrame {
    uint32_t index = 0;          // numéro de l'image (croissant)
    double vsyncSeconds = 0.0;   // instant d'affichage visé, horloge de SteamVR (secondes)
    float cpuMs = std::numeric_limits<float>::quiet_NaN();  // temps CPU du jeu pour l'image (NaN : inconnu)
    float gpuMs = std::numeric_limits<float>::quiet_NaN();  // temps GPU du jeu, mesuré par SteamVR (NaN : inconnu)
    uint32_t presents = 1;       // nombre d'affichages de l'image (plus d'un : reprise par SteamVR, le jeu était en retard)
};

struct VrSystemInfo {
    std::string headset;     // modèle du casque, en ASCII
    std::string runtime;     // "SteamVR 2.x.y"
    float refreshHz = 0.f;   // fréquence d'affichage du casque
    int recommendedWidth = 0, recommendedHeight = 0;  // résolution par œil recommandée par SteamVR
};

enum class VrOverlay { Panel = 0, Watch = 1, Window = 2, Count = 3 };

class VrBackend {
  public:
    virtual ~VrBackend() = default;

    // --- Environnement, sans SteamVR -----------------------------------------------------------
    virtual double now() = 0;                             // horloge monotone, en secondes
    virtual bool runtimeRunning() = 0;                    // SteamVR est lancé (sans jamais le démarrer)
    // Depuis combien de secondes le serveur de SteamVR tourne (le plus récent s'il y en a plusieurs) ;
    // négatif s'il ne tourne pas ou si son heure de lancement est illisible.
    virtual double runtimeAge() = 0;
    virtual std::string processName(uint32_t pid) = 0;    // "Game.exe", vide si inconnu
    virtual bool layerMeasures(uint32_t pid) = 0;         // la layer OpenXR mesure dans ce processus
    virtual bool layerDisplays(uint32_t pid) = 0;         // ... et y affiche le panneau elle-même

    // --- Connexion à SteamVR ---------------------------------------------------------------------
    virtual bool connect() = 0;          // en tant qu'application « overlay » ; écrit la cause d'un échec au journal
    virtual void disconnect() = 0;
    virtual bool quitRequested() = 0;    // traite les événements ; true : SteamVR se ferme, se déconnecter
    virtual VrSystemInfo systemInfo() = 0;
    virtual uint32_t sceneProcess() = 0;  // processus du jeu au premier plan, 0 s'il n'y en a pas
    // Dernières images connues du compositeur, de la plus ancienne à la plus récente.
    virtual int frameTimings(VrFrame* out, int capacity) = 0;
    // Poignée de la manette `hand` (0 gauche, 1 droite) dans le repère de la tête, même repère que la
    // poignée OpenXR (« grip ») ; false si la manette est absente ou n'est pas suivie.
    virtual bool gripInHead(int hand, Pose& out) = 0;
    virtual bool battery(int hand, float& level, bool& charging) = 0;
    // Tête dans le repère de la pièce (« standing » de SteamVR, +Y vers le haut) ; false si elle n'est pas suivie.
    virtual bool headInWorld(Pose& out) = 0;

    // --- Calques (overlays SteamVR) --------------------------------------------------------------
    virtual bool createOverlays() = 0;
    virtual void destroyOverlays() = 0;
    // Image RGBA (ou BGRA si `bgra`), alpha prémultiplié, première ligne en haut, lignes contiguës.
    virtual void setPixels(VrOverlay overlay, const uint8_t* pixels, int width, int height, bool bgra = false) = 0;
    virtual void setWidth(VrOverlay overlay, float meters) = 0;
    virtual void placeOnHead(VrOverlay overlay, const Pose& pose) = 0;
    // `inGrip` : pose dans le repère de la poignée. false si la manette est absente.
    virtual bool placeOnHand(VrOverlay overlay, int hand, const Pose& inGrip) = 0;
    // Pose dans le repère de la pièce (le même que headInWorld).
    virtual void placeInWorld(VrOverlay overlay, const Pose& pose) = 0;
    virtual void show(VrOverlay overlay, bool visible) = 0;
    // Change quand les images envoyées ont été perdues (textures recréées) : elles sont alors à renvoyer.
    virtual uint32_t imageGeneration() { return 0; }
};

}  // namespace xrpo
