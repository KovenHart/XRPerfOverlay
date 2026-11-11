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
constexpr uint32_t kSharedBlockVersion = 2;  // 2 : puissance du processeur et de la carte graphique

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
    float cpuWatts;        // 56 : puissance du processeur (boîtier), W
    float gpuWatts;        // 60 : puissance de la carte graphique, W
};                         // 64 octets ; NaN = valeur indisponible

static_assert(sizeof(SensorBlock) == 64, "la disposition doit rester identique a celle de Program.cs");

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
constexpr uint32_t kSettingsBlockVersion = 8;  // 2 : accroche aux manettes et montre ; 3 : fenêtre capturée ; 4 : langue ; 5 : mode des rapports ;
                                               // 6 : puissance à la place de la fréquence ; 7 : filtres d'image ;
                                               // 8 : faux HDR, raccourcis modifiables, limite et portée de la netteté

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
    // Dernière colonne du tableau CPU / GPU (version 6).
    uint32_t power;             // 192 : 0 = fréquence (MHz), 1 = puissance (W)
    uint32_t reserved6;         // 196
    // Filtres d'image (version 7), mêmes valeurs que ImageSettings (image_params.h).
    int32_t imageMode;          // 200 : 0 aucun, 1 casque, 2 vidéo seulement
    int32_t imageSharpen;       // 204 : 0 aucune, 1 CAS, 2 RCAS, 3 LumaSharpen
    int32_t imageSharpness;     // 208 : 0 à 100
    int32_t imageBrightness;    // 212 : -50 à 50
    int32_t imageContrast;      // 216 : -50 à 50
    int32_t imageSaturation;    // 220 : -100 à 100
    int32_t imageVibrance;      // 224 : 0 à 100
    int32_t imageGamma;         // 228 : 50 à 200
    int32_t imageShadows;       // 232 : 0 à 100
    int32_t imageTemperature;   // 236 : -50 à 50
    int32_t imageDeband;        // 240 : 0 à 100
    int32_t imageColorblind;    // 244 : 0 aucun, 1 protanopie, 2 deutéranopie, 3 tritanopie
    // Version 8.
    int32_t imageHdr;           // 248 : faux HDR, 0 à 100
    // Raccourcis : modificateurs << 16 | touche (code de touche virtuelle) ; modificateurs comme RegisterHotKey
    // (1 Alt, 2 Ctrl, 4 Maj, 8 Windows) ; 0 = sans raccourci. Ordre de Hotkeys::kNames (hotkeys.h).
    int32_t hotkeys[11];        // 252 à 292
    int32_t imageSharpenLimit;  // 296 : LumaSharpen, écart maximal en % de luminosité (1 à 50)
    int32_t imageSharpenRadius; // 300 : portée de la netteté, 1 à 3 pixels
};                              // 304 octets

static_assert(sizeof(SettingsBlock) == 304, "la disposition doit rester identique a celle de OverlaySettings.cs");

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
constexpr uint32_t kVideoShareVersion = 4;  // 2 : textures de la taille de la swapchain, infos par texture ; 3 : panneau dans la vidéo ;
                                           // 4 : orientation de l'œil et de la tête (stabilisation)
constexpr int kVideoSlots = 3;  // textures partagées : la plus récente, une en lecture, une en écriture

// Panneau de performances dans la vidéo (overlay, demandé par l'enregistreur) : un jeu OpenXR ne met pas le
// panneau dans l'image de l'œil (le runtime l'ajoute après), la layer fournit donc son image (après
// l'en-tête, à kVideoPanelOffset) et, avec chaque image du jeu, où il se trouve. L'image miroir de SteamVR
// le contient déjà : XRPerfVR ne fournit rien.
enum VideoOverlayMode : uint32_t { kVideoOverlayOff = 0, kVideoOverlayPlace = 1, kVideoOverlayCorner = 2 };
constexpr int kVideoPanelMaxWidth = 640;
constexpr int kVideoPanelMaxHeight = 320;
constexpr size_t kVideoPanelOffset = 4096;
// panelFlags (par image) : le panneau est affiché dans le casque ; ses coins sont connus (mode « à sa place »).
constexpr uint32_t kVideoPanelShown = 1, kVideoPanelPlaced = 2;

// Filtres d'image du casque (imageState, par image) : pas de filtres dans le casque pour cette image (la
// vidéo n'en reçoit que si l'enregistreur les applique lui-même, mode « vidéo seulement ») ; image déjà
// filtrée ; filtres appliqués plus bas dans la chaîne que la copie (l'enregistreur les applique).
enum VideoImageState : uint32_t { kVideoImageNone = 0, kVideoImageFiltered = 1, kVideoImageLater = 2 };

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
    uint32_t panelFlags;         // 44 : kVideoPanelShown, kVideoPanelPlaced
    // 48 : coins du panneau (haut gauche, haut droit, bas gauche, bas droit), en mètres dans le repère de
    // l'œil de cette image (x à droite, y en haut, -z devant), quand kVideoPanelPlaced.
    float panelCorners[12];
    // 96 : orientations (quaternions x, y, z, w) de l'œil et de la tête dans le repère de la scène (y vers
    // le haut), pour stabiliser la vidéo ; poseValid = 0 si inconnues.
    float eyeOrientation[4];
    float headOrientation[4];    // 112
    uint32_t poseValid;          // 128
    uint32_t imageState;         // 132 : filtres d'image du casque et cette image (VideoImageState)
    uint32_t reserved[2];        // 136
};                               // 144 octets

static_assert(sizeof(VideoSlotInfo) == 144, "disposition fixe");

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
    uint16_t game[64];           // 544 : nom de l'exécutable du jeu, sans « .exe » (UTF-16)
    // --- Panneau dans la vidéo -------------------------------------------------------------------
    uint32_t overlay;            // 672 : VideoOverlayMode, écrit par l'enregistreur
    uint32_t panelSequence;      // 676 : impair pendant que la layer écrit l'image du panneau
    uint32_t panelWidth;         // 680 : taille de l'image du panneau (0 : pas d'image)
    uint32_t panelHeight;        // 684
    uint32_t reserved2[4];       // 688
};                               // 704 octets, puis (à kVideoPanelOffset) l'image du panneau : panelWidth x
                                 // panelHeight pixels RGBA 8 bits (sRGB, alpha prémultiplié, opacité du casque),
                                 // lignes de kVideoPanelMaxWidth pixels

static_assert(sizeof(VideoShareHeader) == 704, "la disposition doit rester identique a celle de VideoRecorder.cs");
static_assert(sizeof(VideoShareHeader) <= kVideoPanelOffset, "l'image du panneau suit l'en-tete");
inline uint64_t VideoLatest(uint64_t frame, int slot) { return (frame << 2) | static_cast<uint64_t>(slot & 3); }
inline uint64_t VideoLatestFrame(uint64_t latest) { return latest >> 2; }
inline int VideoLatestSlot(uint64_t latest) { return static_cast<int>(latest & 3); }
// Durée écoulée depuis `stamp` (GetTickCount64 d'un autre processus, peut-être un peu plus récent que `now`).
inline int64_t VideoAge(uint64_t now, uint64_t stamp) { return static_cast<int64_t>(now - stamp); }
constexpr size_t kVideoShareBytes = kVideoPanelOffset + static_cast<size_t>(kVideoPanelMaxWidth) * kVideoPanelMaxHeight * 4;

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

// --- Source OBS ------------------------------------------------------------------------------------
// Image de la vidéo (œil cadré, stabilisé, filtres, panneau) donnée au plugin OBS xrperf-obs.dll (obs/),
// sans encodage. Le plugin note sa présence tant que la source « XR Perf (casque) » est affichée ;
// XRPerfSensors fait alors tourner l'enregistreur sans fichier (ou en plus d'un enregistrement), qui
// recopie chaque image dans une texture Direct3D 11 partagée (ancien partage par poignée globale, verrou
// « keyed mutex » : l'enregistreur prend la clé 0 et rend la clé 1, le plugin prend 1 et rend 0).
// Créé par XRPerfSensors ou par le plugin, le premier des deux (même descripteur de sécurité).
constexpr wchar_t kObsShareName[] = L"Local\\XRPerfOverlay.Obs";
constexpr uint32_t kObsShareMagic = 0x4F505258;  // "XRPO"
constexpr uint32_t kObsShareVersion = 1;
constexpr uint32_t kObsShareBytes = 128;

struct ObsShare {
    uint32_t magic;            // 0
    uint32_t version;          // 4
    uint64_t consumerTickMs;   // 8  : GetTickCount64() du plugin, renouvelé tant que la source est affichée
    uint64_t producerTickMs;   // 16 : GetTickCount64() de l'enregistreur à la dernière image donnée (0 : rien)
    uint32_t handle;           // 24 : poignée partagée de la texture (R8G8B8A8, sRGB encodé ; 0 : aucune)
    uint32_t width;            // 28
    uint32_t height;           // 32
    uint32_t generation;       // 36 : change quand la texture est recréée
    uint64_t adapterLuid;      // 40 : carte graphique de la texture (LowPart | HighPart << 32)
    uint32_t consumerPid;      // 48 : OBS
    uint32_t producerPid;      // 52 : XRPerfSensors
    uint32_t consumerVersion;  // 56 : version du plugin (majeur << 16 | mineur << 8 | correctif)
    uint32_t reserved[17];     // 60
};                             // 128 octets

static_assert(sizeof(ObsShare) == kObsShareBytes, "disposition fixe (ObsOutput.cs, obs/xrperf_obs.cpp)");

}  // namespace xrpo
