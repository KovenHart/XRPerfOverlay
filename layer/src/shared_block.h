// Blocs de mémoire partagée entre la layer et XRPerfSensors.exe.
// Toute modification doit être reportée dans sensors/Program.cs, sensors/PanelWindow.cs et
// sensors/SettingsWindow.cs (mêmes offsets).
//
// 1. Capteurs : écrit par XRPerfSensors.exe, lu par la layer.
#pragma once

#include <cstdint>

namespace xrpo {

constexpr wchar_t kSharedBlockName[] = L"Local\\XRPerfOverlay.Sensors";
constexpr uint32_t kSharedBlockMagic = 0x4F505258;  // "XRPO"
constexpr uint32_t kSharedBlockVersion = 1;

struct SensorBlock {
    uint32_t magic;        // offset 0
    uint32_t version;      // 4
    uint32_t sequence;     // 8  : impair pendant qu'une écriture est en cours
    uint32_t reserved;     // 12
    uint64_t tickMs;       // 16 : GetTickCount64() au moment de l'écriture
    float cpuLoad;         // 24 : %
    float cpuTemp;         // 28 : °C
    float cpuMHz;          // 32
    float gpuLoad;         // 36 : %
    float gpuTemp;         // 40 : °C
    float gpuMHz;          // 44
    float vramUsedMB;      // 48
    float vramTotalMB;     // 52
};                         // 56 octets ; NaN = valeur indisponible

static_assert(sizeof(SensorBlock) == 56, "la disposition doit rester identique a celle de Program.cs");

// 2. Image du panneau, pour l'afficher dans une fenêtre du bureau (capture OBS) : la mémoire est
//    créée par XRPerfSensors.exe, qui signale qu'il la regarde ; la layer y copie alors le panneau.
constexpr wchar_t kPanelShareName[] = L"Local\\XRPerfOverlay.Panel";
constexpr uint32_t kPanelShareMagic = 0x50505258;  // "XRPP"
constexpr uint32_t kPanelShareVersion = 3;  // 3 : image de 640 x 320 (ligne session / batterie), hauteur utile dans l'en-tête

struct PanelShareHeader {
    uint32_t magic;         // offset 0
    uint32_t version;       // 4
    uint32_t sequence;      // 8  : impair pendant que la layer écrit l'image
    uint32_t contentHeight; // 12 : nombre de lignes dessinées dans l'image, en partant du haut (92 à 320)
    uint64_t readerTickMs;  // 16 : GetTickCount64() de XRPerfSensors tant que sa fenêtre est affichée, sinon 0
    uint64_t writerTickMs;  // 24 : GetTickCount64() de la layer à la dernière image copiée
};                          // 32 octets, suivis de 640 x 320 pixels RGBA 8 bits (alpha prémultiplié)

static_assert(sizeof(PanelShareHeader) == 32, "la disposition doit rester identique a celle de PanelWindow.cs");

// 3. Réglages modifiés dans la fenêtre de XRPerfSensors : écrits par lui, lus par la layer quelques
//    fois par seconde et appliqués dès que `generation` change. Les mêmes réglages sont enregistrés
//    par XRPerfSensors dans XRPerfOverlay.ini, que la layer lit au lancement d'un jeu.
constexpr wchar_t kSettingsBlockName[] = L"Local\\XRPerfOverlay.Settings";
constexpr uint32_t kSettingsBlockMagic = 0x53505258;  // "XRPS"
constexpr uint32_t kSettingsBlockVersion = 3;  // 2 : accroche aux manettes et montre ; 3 : fenêtre capturée

struct SettingsBlock {
    uint32_t magic;             // offset 0
    uint32_t version;           // 4
    uint32_t sequence;          // 8  : impair pendant qu'une écriture est en cours
    uint32_t generation;        // 12 : augmente à chaque changement de réglage
    uint32_t visible;           // 16 : 0 ou 1
    uint32_t position;          // 20 : emplacement, dans l'ordre de l'énumération Corner
    float distance;             // 24 : mètres
    float offsetX;              // 28 : mètres
    float offsetY;              // 32 : mètres
    float width;                // 36 : mètres
    float opacity;              // 40 : fond seul, 0 à 1
    float globalOpacity;        // 44 : ensemble du panneau, 0.2 à 1
    uint32_t faceViewer;        // 48 : 0 ou 1
    uint32_t clock;             // 52 : 0 ou 1
    uint32_t headsetInfo;       // 56 : 0 ou 1
    uint32_t layout;            // 60 : présentation, dans l'ordre de l'énumération Layout
    uint32_t theme;             // 64 : couleurs, dans l'ordre de l'énumération ThemeId
    uint32_t customBackground;  // 68 : 0x00RRGGBB, utilisé par le thème personnalisé
    uint32_t customText;        // 72
    uint32_t customAccent;      // 76
    uint64_t changeTickMs;      // 80 : GetTickCount64() de XRPerfSensors au dernier changement fait par
                                //      l'utilisateur ; 0 tant qu'il n'a rien changé depuis son lancement
    uint32_t anchor;            // 88 : panneau accroché à : énumération Anchor (tête, main gauche, droite)
    uint32_t handPlacement;     // 92 : emplacement sur la main, énumération HandPlacement
    uint32_t gazeOnly;          // 96 : 0 ou 1, sur une main : seulement quand on la regarde
    float handAlong;            // 100 : mètres, ajustement le long de l'avant-bras
    float handLift;             // 104 : mètres, ajustement de l'écart à la main
    uint32_t watchEnabled;      // 108 : 0 ou 1
    uint32_t watchAnchor;       // 112 : énumération Anchor
    uint32_t watchPlacement;    // 116 : énumération HandPlacement
    uint32_t watchStyle;        // 120 : énumération WatchStyle
    uint32_t watchElements;     // 124 : kWatchSession | kWatchBattery | kWatchFps
    uint32_t watchGazeOnly;     // 128 : 0 ou 1
    float watchSize;            // 132 : mètres (diamètre)
    float watchOpacity;         // 136 : 0.2 à 1
    float watchAlong;           // 140 : mètres
    float watchLift;            // 144 : mètres
    uint32_t panelElements;     // 148 : ligne session / batterie du panneau, kWatchSession | kWatchBattery (0 : sans)
    // Fenêtre du bureau capturée et affichée dans le casque (version 3).
    uint32_t windowEnabled;     // 152 : 0 ou 1
    uint32_t windowAnchor;      // 156 : énumération WindowAnchor (main gauche, main droite, posée dans l'espace)
    uint32_t windowPlacement;   // 160 : énumération HandPlacement
    uint32_t windowGazeOnly;    // 164 : 0 ou 1, sur une main : seulement quand on la regarde
    float windowWidth;          // 168 : mètres
    float windowAlong;          // 172 : mètres
    float windowLift;           // 176 : mètres
    float windowDistance;       // 180 : mètres, distance à laquelle Ctrl+Alt+G la pose devant soi
};                              // 184 octets

static_assert(sizeof(SettingsBlock) == 184, "la disposition doit rester identique a celle de SettingsWindow.cs");

// 4. Image d'une fenêtre du bureau (un chat, par exemple), capturée par XRPerfSensors.exe (bibliothèque
//    XRPerfCapture.dll) et affichée dans le casque par la layer ou par XRPerfVR. La mémoire est créée par
//    XRPerfSensors ; celui qui affiche signale qu'il veut l'image (readerTickMs), et la capture ne tourne
//    que pendant ce temps (ou pendant l'aperçu des réglages).
constexpr wchar_t kWindowShareName[] = L"Local\\XRPerfOverlay.Window";
constexpr uint32_t kWindowShareMagic = 0x57505258;  // "XRPW"
constexpr uint32_t kWindowShareVersion = 1;
constexpr int kWindowMaxWidth = 1280;   // image plus grande : réduite à la capture
constexpr int kWindowMaxHeight = 1280;

struct WindowShareHeader {
    uint32_t magic;         // offset 0
    uint32_t version;       // 4
    uint32_t sequence;      // 8  : impair pendant que la capture écrit l'image
    uint32_t frame;         // 12 : augmente à chaque nouvelle image
    uint32_t width;         // 16 : largeur de l'image (0 : pas d'image)
    uint32_t height;        // 20
    uint64_t writerTickMs;  // 24 : GetTickCount64() de la capture à son dernier passage (0 : arrêtée)
    uint64_t readerTickMs;  // 32 : GetTickCount64() de la layer ou de XRPerfVR tant qu'ils veulent l'image
    uint64_t reserved;      // 40
};                          // 48 octets, suivis de width x height pixels BGRA 8 bits (sRGB, alpha
                            // prémultiplié, opacité déjà appliquée), lignes contiguës ; place pour
                            // kWindowMaxWidth x kWindowMaxHeight pixels

static_assert(sizeof(WindowShareHeader) == 48, "la disposition doit rester identique a celle de WindowCapture.cs");
constexpr size_t kWindowShareBytes = sizeof(WindowShareHeader) + static_cast<size_t>(kWindowMaxWidth) * kWindowMaxHeight * 4;

}  // namespace xrpo
