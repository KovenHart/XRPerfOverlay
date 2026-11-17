// Écran tactile au poignet : une grille de raccourcis (vidéo, direct, capture, rapport, image, panneau,
// fenêtre...) affichée sur une main quand on la regarde, et touchée du bout de l'autre manette, comme un
// écran de téléphone. La liste défile d'un glissé vertical ; un appui bref sur une tuile la déclenche.
//
// Aucun bouton n'est lu : seule la position des deux manettes compte (la layer ne prend rien au jeu).
// Commun à la layer OpenXR et à XRPerfVR (SteamVR). Code portable (aucun appel Windows ni graphique),
// testé dans tests/preview.cpp ; le dessin est dans panel.cpp (mêmes outils que le panneau et la montre).
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "panel.h"
#include "pose_math.h"

namespace xrpo {

// Raccourcis de l'écran, dans l'ordre d'affichage. Les valeurs sont celles du bloc de commandes et du
// masque `actions` (bit i = tuile i affichée) : ne jamais les renuméroter, en ajouter à la fin.
enum class TouchAction : int {
    Video = 0,     // enregistrement vidéo : démarrer / arrêter (XRPerfSensors)
    Live,          // direct Twitch / YouTube : démarrer / arrêter (XRPerfSensors)
    Snapshot,      // capture d'écran (XRPerfSensors)
    Report,        // rapport de session en mode manuel : démarrer / arrêter
    Filters,       // filtres d'image : couper / remettre (comme Ctrl+Alt+I)
    Preset,        // preset d'image suivant (XRPerfSensors)
    Panel,         // panneau de performances : afficher / masquer
    PanelPlace,    // panneau devant les yeux : emplacement suivant
    Window,        // fenêtre du bureau : afficher / masquer
    WindowPlace,   // fenêtre du bureau : la poser devant soi
    ResetStats,    // statistiques du panneau remises à zéro
    Count
};
constexpr int kTouchActionCount = static_cast<int>(TouchAction::Count);
constexpr uint32_t kTouchAllActions = (1u << kTouchActionCount) - 1;

// Faite par XRPerfSensors (envoyée par le bloc de commandes), sinon par celui qui affiche l'écran.
inline bool TouchActionRemote(TouchAction a) {
    return a == TouchAction::Video || a == TouchAction::Live || a == TouchAction::Snapshot || a == TouchAction::Preset;
}

// Ce que montrent les tuiles.
struct TouchState {
    Theme theme = PresetTheme(ThemeId::Dark);
    bool english = false;
    int hour = -1, minute = 0;      // heure locale ; hour < 0 : pas d'heure
    bool sensors = false;           // XRPerfSensors répond : vidéo, direct, capture et presets possibles
    bool videoReady = false;        // enregistrement vidéo permis
    bool liveReady = false;         // clé de stream enregistrée
    double videoSeconds = -1.0;     // durée de l'enregistrement ou du direct en cours, < 0 : aucun
    bool live = false;              // ... c'est un direct
    bool reportManual = false;      // rapports en mode manuel (sinon la tuile est inactive)
    bool reportRunning = false;
    double reportSeconds = 0.0;
    bool filtersAllowed = false;    // filtres d'image possibles et réglés (mode casque ou vidéo)
    bool filtersOn = false;         // ... et pas coupés
    char preset[32] = "";           // preset d'image en cours (ASCII), vide : réglages personnels
    bool panelVisible = false;
    bool panelOnView = false;       // panneau devant les yeux : son emplacement peut changer
    bool windowReady = false;       // une fenêtre du bureau est choisie dans XRPerfSensors
    bool windowVisible = false;
};

// Bout de la manette qui touche, dans le repère de l'écran : x, y en pixels de l'image (origine en haut à
// gauche), `depth` en mètres devant la face visible (négatif : derrière, l'écran est traversé).
struct TouchFinger {
    bool valid = false;
    float x = 0.f, y = 0.f, depth = 0.f;
};

// Calcul du point de contact. `fingerInPad` : visée de la manette qui touche (avant de la manette, -Z
// vers où elle pointe ; à défaut sa poignée), dans le repère de l'écran (centre de l'écran, X à droite,
// Y en haut, Z sortant de la face visible) ; `tip` : distance du point de contact devant (m, le long de -Z).
inline TouchFinger FingerOnPad(const Pose& fingerInPad, float widthM, float heightM, int widthPx, int heightPx, float tip) {
    TouchFinger f;
    if (!(widthM > 0.f) || !(heightM > 0.f)) return f;
    const Vec3 p = fingerInPad.position + Rotate(fingerInPad.orientation, {0.f, 0.f, -tip});
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return f;
    f.valid = true;
    f.x = (p.x / widthM + 0.5f) * static_cast<float>(widthPx);
    f.y = (0.5f - p.y / heightM) * static_cast<float>(heightPx);
    f.depth = p.z;
    return f;
}

class TouchPad {
  public:
    // Image : un écran de téléphone, en portrait.
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 400;
    static constexpr int kHeaderHeight = 44;   // heure, témoins
    static constexpr int kMargin = 10;          // bords et écart entre tuiles
    static constexpr int kTileWidth = (kWidth - 3 * kMargin) / 2;
    static constexpr int kTileHeight = 100;
    static constexpr int kViewHeight = kHeight - kHeaderHeight;  // partie qui défile

    // Seuils de contact (m) : le bout de la manette doit arriver par l'avant de l'écran.
    static constexpr float kHoverDepth = 0.15f;     // plus près : le curseur apparaît
    static constexpr float kPressDepth = 0.004f;    // contact
    static constexpr float kReleaseDepth = 0.012f;  // fin du contact (un peu plus loin : pas de rebond)
    static constexpr float kThroughDepth = 0.07f;   // écran traversé de plus : l'appui est abandonné
    static constexpr float kEdge = 40.f;            // marge (px) autour de l'image où le doigt compte encore
    static constexpr float kDragStart = 14.f;       // déplacement (px) au-delà duquel l'appui devient un défilement

    TouchPad() : m_px(static_cast<size_t>(kWidth) * kHeight * 4, 0) { setActions(kTouchAllActions); }

    // Tuiles affichées (bit i = TouchAction i). Garde le défilement dans les bornes.
    void setActions(uint32_t mask) {
        mask &= kTouchAllActions;
        if (mask == m_mask && !m_tiles.empty()) return;
        m_mask = mask;
        m_tiles.clear();
        for (int i = 0; i < kTouchActionCount; ++i)
            if (mask & (1u << i)) m_tiles.push_back(static_cast<TouchAction>(i));
        m_scroll = clampScroll(m_scroll);
        m_pressed = -1;
        m_flash = -1;
    }
    const std::vector<TouchAction>& tiles() const { return m_tiles; }

    // Hauteur totale de la liste et défilement maximal (px).
    int contentHeight() const {
        const int rows = (static_cast<int>(m_tiles.size()) + 1) / 2;
        return kMargin + rows * (kTileHeight + kMargin);
    }
    float maxScroll() const {
        const int extra = contentHeight() - kViewHeight;
        return extra > 0 ? static_cast<float>(extra) : 0.f;
    }
    float scroll() const { return m_scroll; }

    // Tuile sous le point (x, y) de l'image, compte tenu du défilement ; -1 : aucune.
    int tileAt(float x, float y) const {
        if (y < static_cast<float>(kHeaderHeight) || y >= static_cast<float>(kHeight) || x < 0.f || x >= static_cast<float>(kWidth)) return -1;
        const float cy = y - static_cast<float>(kHeaderHeight) + m_scroll - static_cast<float>(kMargin);
        if (cy < 0.f) return -1;
        const int row = static_cast<int>(cy / static_cast<float>(kTileHeight + kMargin));
        if (cy - static_cast<float>(row * (kTileHeight + kMargin)) >= static_cast<float>(kTileHeight)) return -1;  // entre deux rangées
        const float cx = x - static_cast<float>(kMargin);
        if (cx < 0.f) return -1;
        const int col = static_cast<int>(cx / static_cast<float>(kTileWidth + kMargin));
        if (col > 1 || cx - static_cast<float>(col * (kTileWidth + kMargin)) >= static_cast<float>(kTileWidth)) return -1;
        const int index = row * 2 + col;
        return index < static_cast<int>(m_tiles.size()) ? index : -1;
    }
    // Position d'une tuile dans l'image (avant défilement : y relatif au haut de la liste).
    static void tileRect(int index, int& x, int& y) {
        x = kMargin + (index % 2) * (kTileWidth + kMargin);
        y = kMargin + (index / 2) * (kTileHeight + kMargin);
    }

    // À chaque image : position du doigt. Retourne la tuile déclenchée (son action), ou -1.
    int update(const TouchFinger& f, double now) {
        const double dt = m_lastTime < 0.0 ? 0.0 : std::fmin(std::fmax(now - m_lastTime, 0.0), 0.1);
        m_lastTime = now;
        int fired = -1;

        const bool inRange = f.valid && f.x >= -kEdge && f.x < static_cast<float>(kWidth) + kEdge && f.y >= -kEdge &&
                          f.y < static_cast<float>(kHeight) + kEdge && f.depth < kHoverDepth && f.depth > -kThroughDepth;
        if (!inRange) {
            m_pressed = -1;  // doigt parti (ou écran traversé) : l'appui est abandonné
            m_touching = false;
            m_scrolling = false;
            m_hover = false;
        } else if (!m_touching) {
            m_hover = true;
            m_cursorX = f.x;
            m_cursorY = f.y;
            m_cursorDepth = f.depth;
            // Contact : seulement en arrivant par l'avant (une main passée derrière l'écran ne touche rien).
            if (f.depth <= kPressDepth && m_lastDepth > kPressDepth && now >= m_quietUntil) {
                m_touching = true;
                m_scrolling = false;
                m_pressX = f.x;
                m_pressY = f.y;
                m_pressScroll = m_scroll;
                m_velocity = 0.f;
                m_pressed = tileAt(f.x, f.y);
            }
        } else {
            m_cursorX = f.x;
            m_cursorY = f.y;
            m_cursorDepth = f.depth;
            const float dy = f.y - m_pressY;
            if (!m_scrolling && std::fabs(dy) > kDragStart && maxScroll() > 0.f) {
                m_scrolling = true;
                m_pressed = -1;
                m_pressY = f.y;  // le contenu part d'ici : pas de saut au début du glissé
                m_pressScroll = m_scroll;
            }
            if (m_scrolling) {
                const float previous = m_scroll;
                m_scroll = clampScroll(m_pressScroll - (f.y - m_pressY));
                if (dt > 0.0) {
                    const float speed = (m_scroll - previous) / static_cast<float>(dt);
                    m_velocity = m_velocity * 0.6f + speed * 0.4f;
                }
            } else if (m_pressed >= 0 && tileAt(f.x, f.y) != m_pressed) {
                m_pressed = -1;  // le doigt a glissé hors de la tuile : rien ne sera déclenché
            }
            if (f.depth > kReleaseDepth) {
                // Fin du contact : une tuile appuyée puis relâchée se déclenche ; un glissé continue sur sa lancée.
                if (!m_scrolling && m_pressed >= 0) {
                    fired = static_cast<int>(m_tiles[static_cast<size_t>(m_pressed)]);
                    m_flash = m_pressed;
                    m_flashUntil = now + 0.35;
                    m_quietUntil = now + 0.25;
                }
                m_touching = false;
                m_scrolling = false;
                m_pressed = -1;
            }
        }
        m_lastDepth = inRange ? f.depth : -1.f;  // loin ou absent : il faudra repasser devant l'écran pour le toucher

        // Lancée du défilement, freinée.
        if (!m_touching && std::fabs(m_velocity) > 0.f) {
            m_scroll = clampScroll(m_scroll + m_velocity * static_cast<float>(dt));
            m_velocity *= static_cast<float>(std::exp(-4.0 * dt));
            if (std::fabs(m_velocity) < 20.f || m_scroll <= 0.f || m_scroll >= maxScroll()) m_velocity = 0.f;
        }
        if (m_flash >= 0 && now >= m_flashUntil) m_flash = -1;
        return fired;
    }

    // Le doigt est près de l'écran, le touche, ou une animation est en cours : l'image change à chaque
    // image (sinon, seulement quand l'état affiché change).
    bool active() const { return m_hover || m_touching || m_velocity != 0.f || m_flash >= 0; }
    bool touching() const { return m_touching; }

    // Dessin (panel.cpp). `opacity` : de toute l'image, 0 à 1.
    void render(const TouchState& s, float opacity);

    // kWidth x kHeight pixels RGBA 8 bits, alpha prémultiplié, couleurs sRGB.
    const uint8_t* pixels() const { return m_px.data(); }

  private:
    float clampScroll(float v) const {
        const float top = maxScroll();
        return v < 0.f ? 0.f : (v > top ? top : v);
    }

    std::vector<uint8_t> m_px;
    std::vector<TouchAction> m_tiles;
    uint32_t m_mask = 0;
    float m_scroll = 0.f, m_velocity = 0.f;
    bool m_hover = false, m_touching = false, m_scrolling = false;
    float m_cursorX = 0.f, m_cursorY = 0.f, m_cursorDepth = 1.f;
    float m_pressX = 0.f, m_pressY = 0.f, m_pressScroll = 0.f;
    float m_lastDepth = -1.f;
    int m_pressed = -1;       // tuile sous le doigt depuis le contact (-1 : aucune, ou défilement)
    int m_flash = -1;         // tuile qui vient d'être déclenchée (éclat bref)
    double m_flashUntil = 0.0, m_quietUntil = 0.0, m_lastTime = -1.0;
};

}  // namespace xrpo
