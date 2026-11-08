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
constexpr uint32_t kSettingsBlockVersion = 5;  // 2 : accroche aux manettes et montre ; 3 : fenêtre capturée ; 4 : langue ; 5 : mode des rapports

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
    // Langue des textes du panneau, de la montre et des rapports (version 4).
    uint32_t language;          // 184 : 0 = celle de Windows, 1 = français, 2 = anglais
    // Rapports de session (version 5) ; la taille reste un multiple de 8, à cause de changeTickMs.
    uint32_t reportMode;        // 188 : 0 = celui du .ini, 1 = automatique, 2 = manuel (Ctrl+Alt+S), 3 = aucun
};                              // 192 octets

static_assert(sizeof(SettingsBlock) == 192, "la disposition doit rester identique a celle de OverlaySettings.cs");

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

// 5. Enregistrement vidéo du casque. Le bloc est créé par XRPerfSensors.exe (sensors/VideoRecorder.cs) ;
//    l'enregistreur (XRPerfRecord.dll, video_record.cpp) y demande des images, et celui qui les fournit
//    (la layer dans un jeu OpenXR, ou XRPerfVR dans un jeu SteamVR) y décrit des textures Direct3D
//    partagées, nommées d'après son processus (voir VideoTextureName), où il copie l'image d'un œil.
//    L'image ne passe jamais par le processeur : copie dans la carte graphique, puis conversion et
//    encodage par l'enregistreur.
constexpr wchar_t kVideoShareName[] = L"Local\\XRPerfOverlay.Video";
constexpr uint32_t kVideoShareMagic = 0x56505258;  // "XRPV"
constexpr uint32_t kVideoShareVersion = 2;  // 2 : textures de la taille de la swapchain, infos par texture
constexpr int kVideoSlots = 3;  // textures partagées : la plus récente, une en lecture, une en écriture

// Qui fournit les images (producerKind).
enum VideoProducerKind : uint32_t { kVideoNone = 0, kVideoOpenXrD3D11 = 1, kVideoOpenXrD3D12 = 2, kVideoSteamVr = 3 };

// Ce qui accompagne chaque image (un par texture partagée) : écrit avant la publication de l'image, sous
// la même protection que la texture elle-même (jamais réécrit tant qu'elle est la dernière ou en lecture).
struct VideoSlotInfo {
    uint32_t rectX, rectY;       // 0  : partie de la texture qui contient l'image de l'œil
    uint32_t rectWidth, rectHeight;
    float tanLeft, tanRight;     // 16 : bords de cette partie, en tangentes d'angle (gauche négatif)
    float tanUp, tanDown;        // 24 : (bas négatif)
    float centerX, centerY;      // 32 : direction « droit devant » de la tête, mêmes unités (0, 0 sans écran incliné)
    uint32_t linear;             // 40 : 1 si les valeurs sont linéaires (à convertir en sRGB pour la vidéo)
    uint32_t reserved;           // 44
};                               // 48 octets

static_assert(sizeof(VideoSlotInfo) == 48, "disposition fixe");

struct VideoShareHeader {
    uint32_t magic;              // offset 0
    uint32_t version;            // 4
    // --- Écrit par l'enregistreur ---------------------------------------------------------------
    uint32_t wanted;             // 8   : 1 tant qu'un enregistrement attend des images
    uint32_t eye;                // 12  : 0 = œil gauche, 1 = œil droit
    float fps;                   // 16  : cadence de la vidéo (images par seconde)
    uint32_t session;            // 20  : augmente à chaque enregistrement démarré
    uint64_t recorderTickMs;     // 24  : GetTickCount64() de l'enregistreur, renouvelé tant qu'il tourne
    uint64_t recordStartTickMs;  // 32  : GetTickCount64() au début de l'écriture du fichier (0 : pas encore)
    // --- Écrit par le fournisseur d'images ------------------------------------------------------
    uint32_t producerPid;        // 40  : processus qui fournit les images (0 : personne) ; pris par échange atomique
    uint32_t producerKind;       // 44  : VideoProducerKind
    uint64_t producerTickMs;     // 48  : GetTickCount64() du fournisseur à sa dernière image
    uint32_t generation;         // 56  : change quand les textures sont recréées (taille, format) : leurs noms changent
    uint32_t width;              // 60  : taille des textures partagées
    uint32_t height;             // 64
    uint32_t format;             // 68  : DXGI_FORMAT des textures (peut être « typeless »)
    uint64_t adapterLuid;        // 72  : carte graphique (LowPart | HighPart << 32), la même pour l'enregistreur
    // Dernière image publiée, en une seule écriture de 64 bits : numéro << 2 | texture (0 : aucune). Le
    // numéro est aussi la valeur que le fence partagé atteint quand la copie est faite.
    uint64_t latest;             // 80
    uint64_t latestQpc;          // 88  : QueryPerformanceCounter() à la publication
    uint32_t slotBusy[kVideoSlots];  // 96 : 1 pendant que l'enregistreur lit cette texture (le fournisseur n'y écrit pas)
    uint32_t reserved;           // 108
    VideoSlotInfo slots[kVideoSlots];  // 112
    uint16_t game[64];           // 256 : nom de l'exécutable du jeu, sans « .exe » (UTF-16)
};                               // 384 octets

static_assert(sizeof(VideoShareHeader) == 384, "la disposition doit rester identique a celle de VideoRecorder.cs");
inline uint64_t VideoLatest(uint64_t frame, int slot) { return (frame << 2) | static_cast<uint64_t>(slot & 3); }
inline uint64_t VideoLatestFrame(uint64_t latest) { return latest >> 2; }
inline int VideoLatestSlot(uint64_t latest) { return static_cast<int>(latest & 3); }
// Durée écoulée depuis `stamp` (GetTickCount64 d'un autre processus, peut-être un peu plus récent que `now`).
inline int64_t VideoAge(uint64_t now, uint64_t stamp) { return static_cast<int64_t>(now - stamp); }
constexpr size_t kVideoShareBytes = 4096;

// Nom des objets Direct3D partagés : textures (slot 0 à kVideoSlots - 1) et fence (slot -1).
inline void VideoTextureName(wchar_t* out, size_t capacity, uint32_t pid, uint32_t generation, int slot) {
    static const wchar_t kDigits[] = L"0123456789";
    const wchar_t prefix[] = L"XRPerfOverlay.Video.";
    size_t n = 0;
    for (const wchar_t* p = prefix; *p && n + 1 < capacity; ++p) out[n++] = *p;
    auto number = [&](uint32_t v) {
        wchar_t tmp[12];
        int k = 0;
        do {
            tmp[k++] = kDigits[v % 10];
            v /= 10;
        } while (v && k < 12);
        while (k > 0 && n + 1 < capacity) out[n++] = tmp[--k];
    };
    number(pid);
    if (n + 1 < capacity) out[n++] = L'.';
    number(generation);
    if (n + 1 < capacity) out[n++] = L'.';
    if (slot < 0) {
        if (n + 1 < capacity) out[n++] = L'f';
    } else {
        number(static_cast<uint32_t>(slot));
    }
    out[n < capacity ? n : capacity - 1] = L'\0';
}

}  // namespace xrpo
