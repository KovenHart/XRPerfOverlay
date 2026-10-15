// Réglages lus dans XRPerfOverlay.ini (à côté de la DLL).
#pragma once

#include <string>
#include <vector>

#include "panel.h"
#include "pose_math.h"

namespace xrpo {

enum class Corner { TopLeft, Top, TopRight, BottomRight, Bottom, BottomLeft, Count };

// À quoi un calque est accroché : la tête (devant les yeux) ou l'une des manettes.
enum class Anchor { View = 0, LeftHand, RightHand, Count };

// Bornes des réglages de position sur la main (mètres) et de la montre.
constexpr float kMinHandAlong = -0.12f, kMaxHandAlong = 0.12f;
constexpr float kMinHandLift = -0.05f, kMaxHandLift = 0.10f;
constexpr float kMinWatchSize = 0.03f, kMaxWatchSize = 0.20f;
constexpr float kMinWatchOpacity = 0.2f, kMaxWatchOpacity = 1.f;

// Montre : deuxième calque, indépendant du panneau.
struct WatchConfig {
    bool enabled = false;
    Anchor anchor = Anchor::LeftHand;
    HandPlacement placement = HandPlacement::WristBack;
    WatchStyle style = WatchStyle::Classic;
    uint32_t elements = kWatchAllElements;  // éléments affichés en plus de l'heure
    bool gazeOnly = true;                   // sur une main : seulement quand on la regarde
    float size = 0.06f;                     // diamètre (m)
    float opacity = 1.f;
    float along = 0.f, lift = 0.f;          // ajustements de position sur la main (m)
};

// Fenêtre du bureau capturée (un chat...) : sur une main, ou posée dans l'espace (Ctrl+Alt+G la pose
// devant soi). L'ordre est celui de la liste de XRPerfSensors et des noms du .ini.
enum class WindowAnchor { LeftHand = 0, RightHand, World, Count };

constexpr float kMinWindowWidth = 0.10f, kMaxWindowWidth = 1.50f;
constexpr float kMinWindowDistance = 0.30f, kMaxWindowDistance = 3.0f;

// Troisième calque : l'image vient de XRPerfSensors (capture), voir window_share.h.
struct WindowConfig {
    bool enabled = false;
    WindowAnchor anchor = WindowAnchor::World;
    HandPlacement placement = HandPlacement::Above;
    bool gazeOnly = false;              // sur une main : seulement quand on la regarde
    float width = 0.40f;                // largeur (m) ; la hauteur suit les proportions de la fenêtre
    float along = 0.f, lift = 0.f;      // ajustements de position sur la main (m)
    float distance = 0.80f;             // distance à laquelle Ctrl+Alt+G la pose devant soi (m)
    // Posée dans l'espace : pose dans le repère de la pièce (STAGE en OpenXR, « standing » de SteamVR),
    // enregistrée dans le .ini. Pas encore posée : elle l'est devant soi à son premier affichage.
    bool worldSet = false;
    Pose world;
};

// Bornes des réglages modifiables en jeu.
constexpr float kMinWidth = 0.10f, kMaxWidth = 1.50f;
constexpr float kMinGlobalOpacity = 0.20f, kMaxGlobalOpacity = 1.0f;

struct Config {
    bool visible = true;
    Corner position = Corner::BottomLeft;
    float distance = 1.0f;   // mètres devant les yeux
    float offsetX = 0.34f;   // écart horizontal du centre du panneau par rapport à l'axe du regard (m)
    float offsetY = 0.26f;   // écart vertical (m)
    float width = 0.36f;     // largeur du panneau (m)
    float opacity = 0.82f;   // opacité du fond seul (le texte reste plein)
    float globalOpacity = 1.0f;  // opacité de tout le panneau, texte compris
    bool faceViewer = true;  // oriente le panneau vers les yeux
    // Panneau accroché à une main (sinon devant les yeux, réglages ci-dessus).
    Anchor anchor = Anchor::View;
    HandPlacement handPlacement = HandPlacement::Above;
    bool gazeOnly = false;              // sur une main : seulement quand on la regarde
    float handAlong = 0.f, handLift = 0.f;
    WatchConfig watch;
    WindowConfig window;
    int refreshMs = 250;
    int statsWindowS = 30;
    int keyToggle = 'O';     // avec Ctrl+Alt ; 0 = désactivé
    int keyPosition = 'P';
    int keyReset = 'R';
    int keySizeUp = 0x21;       // Page précédente
    int keySizeDown = 0x22;     // Page suivante
    int keyOpacityUp = 0x24;    // Début (Home)
    int keyOpacityDown = 0x23;  // Fin (End)
    int keyWindowToggle = 'F';  // fenêtre capturée : afficher / masquer
    int keyWindowPlace = 'G';   // fenêtre capturée : la poser devant soi
    std::vector<std::wstring> exclude;  // noms d'exécutables à ignorer (en minuscules)
    bool clock = true;                  // afficher l'heure dans le panneau
    bool headsetInfo = true;            // ligne "casque et résolution de rendu" en bas du panneau
    uint32_t panelElements = 0;         // ligne "session / batterie" du panneau : kWatchSession | kWatchBattery
    Layout layout = Layout::Classic;    // présentation du panneau
    ThemeId theme = ThemeId::Dark;      // couleurs
    Rgb customBackground = {14, 17, 22};  // les trois couleurs du thème personnalisé
    Rgb customText = {240, 243, 248};
    Rgb customAccent = {150, 190, 240};
    std::string headsetName;            // nom à afficher à la place de celui annoncé par le runtime (ASCII)
    // Mesure de la cadence après génération d'images : -1 = automatique (seulement si un outil de
    // génération d'images connu est chargé sous notre layer), 1 = toujours, 0 = jamais.
    int measureOutput = -1;
    bool summaryEnabled = true;         // écrire un résumé texte par session
    bool summaryHardware = true;        // modèles du processeur et de la carte graphique dans le résumé
    bool gpuTiming = false;             // mesurer le temps GPU par horodatages (sinon : estimation seule)
    std::wstring summaryFolder;         // dossier des résumés (par défaut : "sessions" à côté de la DLL)
    std::wstring iniPath;
};

// Le suivi des manettes doit être mis en place au lancement du jeu : il l'est si l'un des calques
// est (ou peut être) accroché à une main.
// La fenêtre capturée peut être affichée en cours de partie (Ctrl+Alt+F) : son accroche suffit.
inline bool WantsHandTracking(const Config& c) {
    return c.anchor != Anchor::View || (c.watch.enabled && c.watch.anchor != Anchor::View) || c.window.anchor != WindowAnchor::World;
}
// Batterie des manettes demandée pour la montre ou pour la ligne de session du panneau.
inline bool WantsBattery(const Config& c) {
    return (c.watch.enabled && (c.watch.elements & kWatchBattery) != 0) || (c.panelElements & kWatchBattery) != 0;
}

// `moduleHandle` : handle de la DLL, pour retrouver son dossier.
Config LoadConfig(void* moduleHandle);
// Même lecture, pour le .ini du dossier `folder` (terminé ou non par une barre oblique inverse).
Config LoadConfigFromFolder(const std::wstring& folder);

// Mémorise l'état modifié par les raccourcis (sans effet si le dossier n'est pas accessible en écriture).
void SaveState(const Config& config);

// Nom de l'exécutable courant (ex. "Game-Win64-Shipping.exe"), en minuscules par défaut.
std::wstring CurrentExeName(bool lowercase = true);

// Empêche Windows de décharger notre DLL avant la fin du programme. Nécessaire dès qu'une autre
// layer a reçu des pointeurs vers nos fonctions de sortie de chaîne : elle peut les appeler après
// que le chargeur OpenXR nous a libérés.
void PinModule(void* moduleHandle);

// Dossier de secours pour les résumés : %LOCALAPPDATA%\XRPerfOverlay\sessions (vide si introuvable).
std::wstring FallbackSummaryFolder();

// Conversion UTF-16 -> UTF-8 (pour le journal).
std::string Narrow(const std::wstring& text);

}  // namespace xrpo
