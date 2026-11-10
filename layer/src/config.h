// Réglages lus dans XRPerfOverlay.ini (à côté de la DLL).
#pragma once

#include <string>
#include <vector>

#include "image_params.h"
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

// Rapports de session : un par session (automatique), seulement entre deux appuis sur Ctrl+Alt+S
// (manuel), ou aucun. L'ordre est celui des noms du .ini et de la liste de XRPerfSensors.
enum class ReportMode { Auto = 0, Manual, Off, Count };

// Bornes des réglages modifiables en jeu.
constexpr float kMinWidth = 0.10f, kMaxWidth = 1.50f;
constexpr float kMinGlobalOpacity = 0.20f, kMaxGlobalOpacity = 1.0f;

// Raccourci (modificateurs << 16 | touche) lu dans le .ini ou reçu de XRPerfSensors. Retourne 0 pour « sans
// raccourci », -1 si invalide : touche de modificateur, modificateur inconnu, ou touche sans modificateur
// autre que F1 à F24 (une lettre seule se déclencherait en tapant du texte).
inline int NormalizeHotkey(int value) {
    if (value == 0) return 0;
    if (value < 0) return -1;
    const int mods = value >> 16, key = value & 0xFFFF;
    if (mods > 0xF || key < 0x08 || key > 0xFE) return -1;
    if (key == 0x10 || key == 0x11 || key == 0x12 || key == 0x5B || key == 0x5C || (key >= 0xA0 && key <= 0xA5)) return -1;
    if (mods == 0 && !(key >= 0x70 && key <= 0x87)) return -1;
    return value;
}

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
    // Raccourcis : modificateurs << 16 | touche (code de touche virtuelle) ; modificateurs : 1 Alt, 2 Ctrl,
    // 4 Maj, 8 Windows (comme RegisterHotKey) ; 0 = désactivé. Par défaut Ctrl+Alt+touche.
    static constexpr int kCtrlAlt = 3 << 16;
    int keyToggle = kCtrlAlt | 'O';
    int keyPosition = kCtrlAlt | 'P';
    int keyReset = kCtrlAlt | 'R';
    int keySizeUp = kCtrlAlt | 0x21;       // Page précédente
    int keySizeDown = kCtrlAlt | 0x22;     // Page suivante
    int keyOpacityUp = kCtrlAlt | 0x24;    // Début (Home)
    int keyOpacityDown = kCtrlAlt | 0x23;  // Fin (End)
    int keyWindowToggle = kCtrlAlt | 'F';  // fenêtre capturée : afficher / masquer
    int keyWindowPlace = kCtrlAlt | 'G';   // fenêtre capturée : la poser devant soi
    int keyRecord = kCtrlAlt | 'S';        // rapport en mode manuel : démarrer / arrêter l'enregistrement
    int keyImage = kCtrlAlt | 'I';         // filtres d'image : couper / remettre (comparaison en jeu)
    // Raccourci n° i (0 à kHotkeyCount - 1), dans l'ordre du bloc partagé, du .ini et de XRPerfSensors :
    // toggle, position, reset, size_up, size_down, opacity_up, opacity_down, window_toggle, window_place,
    // record, image.
    static constexpr int kHotkeyCount = 11;
    int& hotkey(int i) {
        int* const keys[kHotkeyCount] = {&keyToggle,      &keyPosition,     &keyReset,       &keySizeUp, &keySizeDown, &keyOpacityUp,
                                         &keyOpacityDown, &keyWindowToggle, &keyWindowPlace, &keyRecord, &keyImage};
        return *keys[i];
    }
    std::vector<std::wstring> exclude;  // noms d'exécutables à ignorer (en minuscules)
    bool clock = true;                  // afficher l'heure dans le panneau
    bool headsetInfo = true;            // ligne "casque et résolution de rendu" en bas du panneau
    uint32_t panelElements = 0;         // ligne "session / batterie" du panneau : kWatchSession | kWatchBattery
    bool showPower = false;             // dernière colonne du tableau CPU / GPU : puissance (W) au lieu de la fréquence
    Layout layout = Layout::Classic;    // présentation du panneau
    ThemeId theme = ThemeId::Dark;      // couleurs
    Rgb customBackground = {14, 17, 22};  // les trois couleurs du thème personnalisé
    Rgb customText = {240, 243, 248};
    Rgb customAccent = {150, 190, 240};
    std::string headsetName;            // nom à afficher à la place de celui annoncé par le runtime (ASCII)
    // Textes du panneau, de la montre et des rapports en anglais (sinon en français). Dans le .ini :
    // language = auto (langue de Windows : français pour un Windows en français, anglais sinon), fr ou en.
    bool english = false;
    // Mesure de la cadence après génération d'images : -1 = automatique (seulement si un outil de
    // génération d'images connu est chargé sous notre layer), 1 = toujours, 0 = jamais.
    int measureOutput = -1;
    ReportMode reportMode = ReportMode::Auto;  // quand écrire un rapport de session
    bool summaryHardware = true;        // modèles du processeur et de la carte graphique dans le résumé
    bool gpuTiming = false;             // mesurer le temps GPU par horodatages (sinon : estimation seule)
    // Enregistrement vidéo possible dans ce jeu ([video] capture) : la layer suit alors les images des
    // swapchains du jeu pour pouvoir les copier quand XRPerfSensors enregistre. 0 : rien de tout ça.
    bool videoCapture = true;
    // Image enregistrée quand d'autres layers sont sous la nôtre : true = celle envoyée au casque, après
    // leur traitement (Pimax NR, OpenXR Toolkit...) ; false = celle du jeu.
    bool videoFinal = true;
    // Filtres d'image possibles dans ce jeu ([image] enabled) : la layer suit alors les swapchains du jeu
    // pour pouvoir filtrer ses images. Les filtres eux-mêmes (image) sont désactivés par défaut.
    bool imageAllowed = true;
    ImageSettings image;
    bool imageBypass = false;           // filtres coupés au raccourci (le temps de la session)
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
