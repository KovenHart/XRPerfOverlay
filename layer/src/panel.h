// Dessin du panneau dans un tampon RGBA en mémoire (aucun appel graphique : le résultat est
// simplement copié dans une texture). Code portable, testable hors VR (voir tests/preview.cpp).
#pragma once

#include <cstdint>
#include <limits>
#include <vector>

namespace xrpo {

struct Rgb {
    uint8_t r, g, b;
};

// Jeu de couleurs du panneau.
struct Theme {
    Rgb background;  // fond
    Rgb text;        // valeurs
    Rgb muted;       // intitulés et unités
    Rgb accent;      // titres (CPU, GPU, VRAM), courbe du graphique
    Rgb line;        // séparateurs, fond des jauges
    Rgb good;        // cadence tenue
    Rgb warn;        // à surveiller
    Rgb bad;         // dépassement
    Rgb gpu;         // courbe GPU de la présentation graphique
};

// Couleurs prédéfinies. L'ordre est celui de la liste de XRPerfSensors et des noms du .ini.
enum class ThemeId { Dark = 0, Light, Contrast, Green, Amber, Violet, Custom, Count };

// Thème prédéfini (identifiant hors bornes ou Custom : le thème sombre).
Theme PresetTheme(ThemeId id);
// Thème construit à partir de trois couleurs choisies ; les autres en sont déduites.
Theme CustomTheme(Rgb background, Rgb text, Rgb accent);

// Présentation du panneau.
enum class Layout {
    Classic = 0,  // FPS + tableau CPU / GPU + VRAM
    Graph,        // FPS + courbe du temps d'image + CPU / GPU sur deux lignes
    Compact,      // FPS seulement
    Count
};

// Toutes les valeurs flottantes peuvent valoir NaN = "indisponible" (affiché "--").
struct PanelData {
    // Largeur de la courbe : une colonne = deux pixels du panneau.
    static constexpr int kGraphColumns = 296;

    bool fpsValid = false;
    float fps = 0.f, fpsAvg = 0.f, fpsMin = 0.f, fpsMax = 0.f;
    float targetFps = 0.f;  // fréquence du casque (0 = inconnue)

    // Images par seconde envoyées au casque quand un outil de génération d'images en ajoute
    // (NaN : pas de génération détectée, rien n'est affiché).
    float outFps = std::numeric_limits<float>::quiet_NaN();
    int clockMinutes = -1;  // heure locale en minutes depuis minuit, -1 = ne pas l'afficher

    float cpuFrameMs = std::numeric_limits<float>::quiet_NaN();
    // Temps GPU affiché : mesuré (option de mesure réelle) ou, à défaut, estimé.
    float gpuFrameMs = std::numeric_limits<float>::quiet_NaN();
    bool gpuMeasured = false;  // false : gpuFrameMs est l'estimation, affichée précédée de "~"
    // Estimation (charge GPU x durée d'une image), toujours calculée quand les capteurs répondent.
    float gpuFrameEstimateMs = std::numeric_limits<float>::quiet_NaN();

    bool sensorsOk = false;  // le programme XRPerfSensors tourne et ses données sont fraîches
    float cpuLoad = std::numeric_limits<float>::quiet_NaN();
    float cpuTemp = std::numeric_limits<float>::quiet_NaN();
    float cpuMHz = std::numeric_limits<float>::quiet_NaN();
    float gpuLoad = std::numeric_limits<float>::quiet_NaN();
    float gpuTemp = std::numeric_limits<float>::quiet_NaN();
    float gpuMHz = std::numeric_limits<float>::quiet_NaN();
    float vramUsedMB = std::numeric_limits<float>::quiet_NaN();
    float vramTotalMB = std::numeric_limits<float>::quiet_NaN();

    // Ligne de session (option) sous le corps du panneau : temps de session et batterie des manettes.
    uint32_t statusElements = 0;  // kWatchSession | kWatchBattery ; 0 : pas de ligne
    double sessionSeconds = -1.0;  // < 0 : inconnu
    float battery[2] = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN()};  // 0 à 1, NaN : inconnue
    bool charging[2] = {false, false};

    // Ligne d'informations en bas du panneau : casque et résolution de rendu.
    bool showInfo = false;  // false : panneau sans cette ligne
    char headset[48] = "";  // nom du casque, en ASCII ; vide = inconnu
    int renderWidth = 0;    // image soumise par le jeu pour un œil, en pixels ; 0 = pas encore connue
    int renderHeight = 0;
    int recommendedWidth = 0;  // résolution recommandée par le runtime pour un œil ; 0 = inconnue
    int recommendedHeight = 0;

    // Apparence.
    Layout layout = Layout::Classic;
    Theme theme = PresetTheme(ThemeId::Dark);
    bool english = false;  // textes en anglais (sinon en français)

    // Témoin d'enregistrement d'un rapport en mode manuel (RecordIndicator de report_control.h) :
    // 0 = rien, 1 = en cours (avec sa durée), 2 = rapport écrit, 3 = trop court, pas de rapport.
    int recordState = 0;
    double recordSeconds = 0.0;

    // Présentation "graphique" : temps d'image des dernières secondes, de la plus ancienne colonne
    // (indice 0) à la plus récente. Valeurs en millisecondes, 0 = pas d'image dans cette tranche.
    bool graphValid = false;  // false : la courbe n'est pas dessinée
    float graphFrameMs[kGraphColumns] = {};
    float graphCpuMs[kGraphColumns] = {};
    float graphGpuMs[kGraphColumns] = {};  // mesuré si gpuMeasured, sinon estimé (0 = inconnu)
};

class Panel {
  public:
    static constexpr int kWidth = 640;
    static constexpr int kHeight = 320;      // hauteur du tampon et de la texture (panneau le plus haut)
    static constexpr int kBodyHeight = 256;  // présentations complète et graphique, sans les lignes du bas
    static constexpr int kCompactHeight = 92;
    static constexpr int kStatusHeight = 32;  // ligne session / batterie (option)
    static constexpr int kInfoHeight = 32;   // ligne casque / résolution

    static constexpr int bodyHeight(Layout layout) { return layout == Layout::Compact ? kCompactHeight : kBodyHeight; }

    // Hauteur réellement dessinée : le reste du tampon est transparent.
    static constexpr int contentHeight(Layout layout, bool showInfo, bool showStatus) {
        return bodyHeight(layout) + (showStatus ? kStatusHeight : 0) + (showInfo ? kInfoHeight : 0);
    }
    static constexpr int contentHeight(const PanelData& d) { return contentHeight(d.layout, d.showInfo, d.statusElements != 0); }

    Panel() : m_px(static_cast<size_t>(kWidth) * kHeight * 4, 0) {}

    // Coût : le décor fixe (fond, intitulés, unités) n'est dessiné qu'une fois puis recopié ;
    // chaque appel ne redessine que les valeurs, soit quelques dizaines de microsecondes.

    // `bgOpacity` : opacité du fond seul, de 0 (invisible) à 1 (opaque).
    // `globalOpacity` : opacité de l'ensemble du panneau, texte compris.
    void render(const PanelData& d, float bgOpacity, float globalOpacity = 1.f);

    // kWidth x kHeight pixels RGBA 8 bits, alpha prémultiplié, couleurs sRGB, origine en haut à gauche.
    const uint8_t* pixels() const { return m_px.data(); }

  private:
    void buildBase(const PanelData& d, int opacity, int scale);

    std::vector<uint8_t> m_px;
    std::vector<uint8_t> m_base;  // décor fixe, valable pour les réglages mémorisés ci-dessous
    int m_baseOpacity = -1;
    int m_baseScale = -1;
    bool m_baseInfo = false;
    uint32_t m_baseStatus = 0;
    Layout m_baseLayout = Layout::Classic;
    Theme m_baseTheme = {};
    bool m_baseEnglish = false;
};

// --- Montre ------------------------------------------------------------------------------------
// Petit cadran séparé du panneau, prévu pour le poignet : heure, temps de session, batterie des
// manettes, FPS. Dessiné de la même façon que le panneau (tampon mémoire RGBA, alpha prémultiplié).

enum class WatchStyle {
    Classic = 0,  // cadran rond à aiguilles
    Digital,      // écran carré, gros chiffres
    Sport,        // cadran rond, heure en chiffres, couronne des secondes
    Count
};

// Éléments facultatifs (l'heure est toujours affichée).
constexpr uint32_t kWatchSession = 1;  // temps écoulé depuis le début de la session
constexpr uint32_t kWatchBattery = 2;  // batterie des manettes
constexpr uint32_t kWatchFps = 4;      // images par seconde
constexpr uint32_t kWatchAllElements = kWatchSession | kWatchBattery | kWatchFps;

struct WatchData {
    WatchStyle style = WatchStyle::Classic;
    uint32_t elements = kWatchAllElements;
    Theme theme = PresetTheme(ThemeId::Dark);
    int hour = -1, minute = 0, second = 0;  // heure locale ; hour < 0 = inconnue
    double sessionSeconds = -1.0;           // < 0 : inconnu (non affiché)
    // Batterie des manettes, de 0 à 1 (NaN = inconnue) : [0] gauche, [1] droite.
    float battery[2] = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN()};
    bool charging[2] = {false, false};
    bool fpsValid = false;
    float fps = 0.f;
    float targetFps = 0.f;
    bool english = false;  // textes en anglais (lettres des manettes L / R au lieu de G / D)
};

class WatchFace {
  public:
    static constexpr int kSize = 320;  // tampon carré ; le cadran le remplit, les coins restent transparents

    WatchFace() : m_px(static_cast<size_t>(kSize) * kSize * 4, 0) {}

    // `opacity` : opacité de toute la montre, de 0 à 1.
    void render(const WatchData& d, float opacity = 1.f);

    // kSize x kSize pixels RGBA 8 bits, alpha prémultiplié, couleurs sRGB.
    const uint8_t* pixels() const { return m_px.data(); }

  private:
    std::vector<uint8_t> m_px;
};

}  // namespace xrpo
