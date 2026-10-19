// Passage des réglages reçus de XRPerfSensors (bloc de mémoire partagée) aux réglages de la layer,
// et noms des présentations et des thèmes dans XRPerfOverlay.ini.
// Code portable (aucune dépendance Windows), testé dans tests/preview.cpp.
#pragma once

#include <cmath>
#include <cstring>

#include "config.h"
#include "panel.h"
#include "shared_block.h"

namespace xrpo {

// Noms utilisés dans le .ini, dans l'ordre des énumérations Layout et ThemeId.
constexpr const char* kLayoutNames[] = {"classic", "graph", "compact"};
constexpr const char* kThemeNames[] = {"dark", "light", "contrast", "green", "amber", "violet", "custom"};
// Dans l'ordre des énumérations Anchor, HandPlacement et WatchStyle.
constexpr const char* kAnchorNames[] = {"view", "left_hand", "right_hand"};
constexpr const char* kPlacementNames[] = {"wrist_back", "wrist_inside", "above"};
constexpr const char* kWatchStyleNames[] = {"classic", "digital", "sport"};
// Dans l'ordre de l'énumération WindowAnchor.
constexpr const char* kWindowAnchorNames[] = {"left_hand", "right_hand", "world"};
// Dans l'ordre de l'énumération ReportMode.
constexpr const char* kReportModeNames[] = {"auto", "manual", "off"};

// Indice du nom dans la liste (comparaison exacte, en minuscules), ou `fallback` s'il est inconnu.
template <size_t N>
int NameIndex(const char* const (&names)[N], const char* text, int fallback) {
    for (size_t i = 0; i < N; ++i)
        if (std::strcmp(names[i], text) == 0) return static_cast<int>(i);
    return fallback;
}

inline Rgb RgbFromPacked(uint32_t packed) {
    return {static_cast<uint8_t>((packed >> 16) & 0xFF), static_cast<uint8_t>((packed >> 8) & 0xFF), static_cast<uint8_t>(packed & 0xFF)};
}

// Lit "#RRGGBB" ou "RRGGBB". Retourne false (et ne touche pas à `out`) si le texte n'est pas une couleur.
inline bool ParseColor(const char* text, Rgb& out) {
    if (*text == '#') ++text;
    if (std::strlen(text) != 6) return false;
    uint32_t value = 0;
    for (int i = 0; i < 6; ++i) {
        const char c = text[i];
        uint32_t digit;
        if (c >= '0' && c <= '9')
            digit = static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = static_cast<uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            digit = static_cast<uint32_t>(c - 'A' + 10);
        else
            return false;
        value = value * 16 + digit;
    }
    out = RgbFromPacked(value);
    return true;
}

// Couleurs effectives pour ces réglages.
inline Theme ThemeOf(const Config& c) {
    return c.theme == ThemeId::Custom ? CustomTheme(c.customBackground, c.customText, c.customAccent) : PresetTheme(c.theme);
}

// Reporte dans `c` les réglages reçus, bornés comme ceux du .ini : une valeur aberrante (NaN,
// indice hors liste) laisse le réglage en cours inchangé ou le ramène dans ses bornes.
inline void ApplySettings(Config& c, const SettingsBlock& s) {
    const auto clamped = [](float value, float lo, float hi, float current) {
        if (std::isnan(value)) return current;
        return value < lo ? lo : (value > hi ? hi : value);
    };
    c.visible = s.visible != 0;
    if (s.position < static_cast<uint32_t>(Corner::Count)) c.position = static_cast<Corner>(s.position);
    c.distance = clamped(s.distance, 0.3f, 5.f, c.distance);
    c.offsetX = clamped(s.offsetX, 0.f, 3.f, c.offsetX);
    c.offsetY = clamped(s.offsetY, 0.f, 3.f, c.offsetY);
    c.width = clamped(s.width, kMinWidth, kMaxWidth, c.width);
    c.opacity = clamped(s.opacity, 0.f, 1.f, c.opacity);
    c.globalOpacity = clamped(s.globalOpacity, kMinGlobalOpacity, kMaxGlobalOpacity, c.globalOpacity);
    c.faceViewer = s.faceViewer != 0;
    c.clock = s.clock != 0;
    c.headsetInfo = s.headsetInfo != 0;
    if (s.layout < static_cast<uint32_t>(Layout::Count)) c.layout = static_cast<Layout>(s.layout);
    if (s.theme < static_cast<uint32_t>(ThemeId::Count)) c.theme = static_cast<ThemeId>(s.theme);
    c.customBackground = RgbFromPacked(s.customBackground);
    c.customText = RgbFromPacked(s.customText);
    c.customAccent = RgbFromPacked(s.customAccent);

    // Accroche aux manettes et montre.
    if (s.anchor < static_cast<uint32_t>(Anchor::Count)) c.anchor = static_cast<Anchor>(s.anchor);
    if (s.handPlacement < static_cast<uint32_t>(HandPlacement::Count)) c.handPlacement = static_cast<HandPlacement>(s.handPlacement);
    c.gazeOnly = s.gazeOnly != 0;
    c.handAlong = clamped(s.handAlong, kMinHandAlong, kMaxHandAlong, c.handAlong);
    c.handLift = clamped(s.handLift, kMinHandLift, kMaxHandLift, c.handLift);
    c.watch.enabled = s.watchEnabled != 0;
    if (s.watchAnchor < static_cast<uint32_t>(Anchor::Count)) c.watch.anchor = static_cast<Anchor>(s.watchAnchor);
    if (s.watchPlacement < static_cast<uint32_t>(HandPlacement::Count)) c.watch.placement = static_cast<HandPlacement>(s.watchPlacement);
    if (s.watchStyle < static_cast<uint32_t>(WatchStyle::Count)) c.watch.style = static_cast<WatchStyle>(s.watchStyle);
    c.watch.elements = s.watchElements & kWatchAllElements;
    c.watch.gazeOnly = s.watchGazeOnly != 0;
    c.watch.size = clamped(s.watchSize, kMinWatchSize, kMaxWatchSize, c.watch.size);
    c.watch.opacity = clamped(s.watchOpacity, kMinWatchOpacity, kMaxWatchOpacity, c.watch.opacity);
    c.watch.along = clamped(s.watchAlong, kMinHandAlong, kMaxHandAlong, c.watch.along);
    c.watch.lift = clamped(s.watchLift, kMinHandLift, kMaxHandLift, c.watch.lift);
    c.panelElements = s.panelElements & (kWatchSession | kWatchBattery);

    // Fenêtre capturée (la pose dans l'espace ne vient que du .ini et du raccourci Ctrl+Alt+G).
    WindowConfig& w = c.window;
    w.enabled = s.windowEnabled != 0;
    if (s.windowAnchor < static_cast<uint32_t>(WindowAnchor::Count)) w.anchor = static_cast<WindowAnchor>(s.windowAnchor);
    if (s.windowPlacement < static_cast<uint32_t>(HandPlacement::Count)) w.placement = static_cast<HandPlacement>(s.windowPlacement);
    w.gazeOnly = s.windowGazeOnly != 0;
    w.width = clamped(s.windowWidth, kMinWindowWidth, kMaxWindowWidth, w.width);
    w.along = clamped(s.windowAlong, kMinHandAlong, kMaxHandAlong, w.along);
    w.lift = clamped(s.windowLift, kMinHandLift, kMaxHandLift, w.lift);
    w.distance = clamped(s.windowDistance, kMinWindowDistance, kMaxWindowDistance, w.distance);

    // Langue : XRPerfSensors envoie la langue effective (1 = français, 2 = anglais) ; 0 laisse celle du .ini.
    if (s.language == 1) c.english = false;
    else if (s.language == 2) c.english = true;
    // Rapports : 1 à 3 = automatique, manuel, aucun ; 0 (ou inconnu) laisse celui du .ini.
    if (s.reportMode >= 1 && s.reportMode <= static_cast<uint32_t>(ReportMode::Count)) c.reportMode = static_cast<ReportMode>(s.reportMode - 1);
}

// Réglages reçus pendant une partie : comme ApplySettings, sauf pour les réglages que les raccourcis
// changent aussi en jeu (panneau affiché, emplacement, taille, opacité ; fenêtre capturée affichée et
// posée) : ceux-là ne sont repris que si la fenêtre de réglages les a changés depuis le bloc précédent.
// Sinon, un bloc publié avant un raccourci annulerait ce raccourci au changement suivant d'un autre réglage.
inline void ApplySettingsUpdate(Config& c, const SettingsBlock& s, const SettingsBlock* previous) {
    const Config before = c;
    ApplySettings(c, s);
    if (!previous) return;
    if (s.visible == previous->visible) c.visible = before.visible;
    if (s.position == previous->position) c.position = before.position;
    if (s.width == previous->width) c.width = before.width;
    if (s.globalOpacity == previous->globalOpacity) c.globalOpacity = before.globalOpacity;
    if (s.windowEnabled == previous->windowEnabled) c.window.enabled = before.window.enabled;
    if (s.windowAnchor == previous->windowAnchor) c.window.anchor = before.window.anchor;
}

// Nombre décimal écrit avec un point (indépendant des réglages régionaux du jeu) : « -1.25 ». Avance
// `text` après le nombre ; false s'il n'y en a pas.
inline bool ParseDecimal(const char*& text, float& out) {
    const char* p = text;
    while (*p == ' ' || *p == '\t') ++p;
    bool negative = false;
    if (*p == '-' || *p == '+') negative = *p++ == '-';
    double value = 0.0, scale = 1.0;
    int digits = 0;
    while (*p >= '0' && *p <= '9' && digits < 12) value = value * 10.0 + (*p++ - '0'), ++digits;
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') {
            if (digits < 15) value = value * 10.0 + (*p - '0'), scale *= 10.0;
            ++p, ++digits;
        }
    }
    if (digits == 0 || (*p >= '0' && *p <= '9')) return false;
    out = static_cast<float>((negative ? -value : value) / scale);
    text = p;
    return true;
}

// Pose dans l'espace enregistrée dans le .ini : « x y z qx qy qz qw » (mètres, quaternion). Retourne
// false si le texte n'en est pas une (valeurs manquantes, non finies, position aberrante).
inline bool ParseWorldPose(const char* text, Pose& out) {
    float v[7];
    const char* p = text;
    for (int i = 0; i < 7; ++i)
        if (!ParseDecimal(p, v[i])) return false;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p != '\0') return false;
    for (int i = 0; i < 3; ++i)
        if (std::fabs(v[i]) > 100.f) return false;
    const float n = std::sqrt(v[3] * v[3] + v[4] * v[4] + v[5] * v[5] + v[6] * v[6]);
    if (!(n > 0.5f && n < 1.5f)) return false;
    out.position = {v[0], v[1], v[2]};
    out.orientation = {v[3] / n, v[4] / n, v[5] / n, v[6] / n};
    return true;
}

}  // namespace xrpo
