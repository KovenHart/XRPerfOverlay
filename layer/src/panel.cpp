#include "panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "font_data.h"

namespace xrpo {

Theme PresetTheme(ThemeId id) {
    // Ordre des couleurs : fond, valeurs, intitulés, accent, traits, bon, moyen, mauvais, courbe GPU.
    static const Theme kPresets[] = {
        /* Sombre     */ {{14, 17, 22}, {240, 243, 248}, {142, 152, 170}, {150, 190, 240}, {70, 78, 94}, {110, 225, 140}, {255, 190, 70}, {255, 100, 90}, {120, 220, 150}},
        /* Clair      */ {{246, 247, 249}, {22, 26, 34}, {96, 106, 122}, {28, 98, 200}, {196, 202, 212}, {22, 140, 66}, {186, 108, 0}, {204, 42, 42}, {20, 140, 70}},
        /* Contraste  */ {{0, 0, 0}, {255, 255, 255}, {205, 205, 205}, {255, 221, 0}, {120, 120, 120}, {0, 255, 128}, {255, 170, 0}, {255, 80, 80}, {0, 230, 255}},
        /* Vert       */ {{5, 14, 8}, {170, 255, 190}, {84, 160, 104}, {60, 230, 110}, {32, 84, 48}, {60, 230, 110}, {255, 200, 60}, {255, 96, 80}, {120, 200, 255}},
        /* Ambre      */ {{20, 13, 5}, {255, 222, 170}, {176, 130, 74}, {255, 176, 0}, {96, 66, 26}, {150, 220, 110}, {255, 150, 40}, {255, 90, 70}, {120, 200, 255}},
        /* Violet     */ {{17, 12, 28}, {242, 238, 252}, {154, 144, 180}, {196, 140, 255}, {74, 60, 102}, {90, 235, 190}, {255, 196, 90}, {255, 96, 130}, {90, 235, 190}},
    };
    const int index = static_cast<int>(id);
    const int count = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));
    return kPresets[index >= 0 && index < count ? index : 0];
}

namespace {

// Mélange : `t` = 0 donne `a`, 255 donne `b`.
Rgb mix(Rgb a, Rgb b, int t) {
    return {static_cast<uint8_t>((a.r * (255 - t) + b.r * t + 127) / 255), static_cast<uint8_t>((a.g * (255 - t) + b.g * t + 127) / 255),
            static_cast<uint8_t>((a.b * (255 - t) + b.b * t + 127) / 255)};
}

}  // namespace

Theme CustomTheme(Rgb background, Rgb text, Rgb accent) {
    Theme theme;
    theme.background = background;
    theme.text = text;
    theme.accent = accent;
    theme.muted = mix(text, background, 105);
    theme.line = mix(background, text, 66);
    // Couleurs d'état : vives sur fond sombre, plus soutenues sur fond clair.
    const int luminance = (background.r * 299 + background.g * 587 + background.b * 114) / 1000;
    const Theme& reference = PresetTheme(luminance > 140 ? ThemeId::Light : ThemeId::Dark);
    theme.good = reference.good;
    theme.warn = reference.warn;
    theme.bad = reference.bad;
    theme.gpu = reference.gpu;
    return theme;
}

namespace {

using Color = Rgb;

// Dessine dans un tampon RGBA prémultiplié avec l'opérateur "over".
class Canvas {
  public:
    // `scale` (0..256) = opacité d'ensemble du panneau. En alpha prémultiplié, un panneau à moitié
    // transparent s'obtient en dessinant chaque couleur (et son alpha) à moitié : le résultat est
    // le même que si l'on atténuait toute l'image après coup, sans passe supplémentaire.
    Canvas(uint8_t* px, int w, int h, int scale) : m_px(px), m_w(w), m_h(h), m_scale(scale) {}

    void blend(int x, int y, Color c, int coverage) {
        if (x < 0 || y < 0 || x >= m_w || y >= m_h || coverage <= 0) return;
        uint8_t* p = m_px + (static_cast<size_t>(y) * m_w + x) * 4;
        const int r = (c.r * m_scale) >> 8, g = (c.g * m_scale) >> 8, b = (c.b * m_scale) >> 8, a = (255 * m_scale) >> 8;
        if (coverage >= 255) {
            p[0] = static_cast<uint8_t>(r);
            p[1] = static_cast<uint8_t>(g);
            p[2] = static_cast<uint8_t>(b);
            p[3] = static_cast<uint8_t>(a);
            return;
        }
        const int inv = 255 - coverage;
        p[0] = static_cast<uint8_t>((r * coverage + p[0] * inv + 127) / 255);
        p[1] = static_cast<uint8_t>((g * coverage + p[1] * inv + 127) / 255);
        p[2] = static_cast<uint8_t>((b * coverage + p[2] * inv + 127) / 255);
        p[3] = static_cast<uint8_t>((a * coverage + p[3] * inv + 127) / 255);
    }

    void fillRect(int x, int y, int w, int h, Color c, int coverage = 255) {
        for (int j = y; j < y + h; ++j)
            for (int i = x; i < x + w; ++i) blend(i, j, c, coverage);
    }

    // Rectangle plein à coins arrondis (bords lissés).
    void fillRoundRect(int x, int y, int w, int h, float radius, Color c, int coverage) {
        const float fw = static_cast<float>(w), fh = static_cast<float>(h), fullCoverage = static_cast<float>(coverage);
        for (int j = 0; j < h; ++j) {
            for (int i = 0; i < w; ++i) {
                // Distance au rectangle "rétréci" de `radius` : > radius = hors du coin arrondi.
                const float cx = static_cast<float>(i) + 0.5f, cy = static_cast<float>(j) + 0.5f;
                const float dx = std::fmax(std::fmax(radius - cx, cx - (fw - radius)), 0.f);
                const float dy = std::fmax(std::fmax(radius - cy, cy - (fh - radius)), 0.f);
                float a = radius + 0.5f - std::sqrt(dx * dx + dy * dy);
                if (a <= 0.f) continue;
                if (a > 1.f) a = 1.f;
                blend(x + i, y + j, c, static_cast<int>(fullCoverage * a + 0.5f));
            }
        }
    }

    static int textWidth(const FontAtlas& f, const char* s) { return static_cast<int>(std::strlen(s)) * f.cellW; }

    // (x, y) = coin haut-gauche de la première cellule. Retourne le x de fin.
    int text(const FontAtlas& f, int x, int y, const char* s, Color c) {
        for (; *s; ++s, x += f.cellW) {
            const int idx = static_cast<unsigned char>(*s) - f.first;
            if (idx < 0 || idx >= f.count) continue;  // caractère absent de l'atlas : simple espace
            const unsigned char* g = f.data + static_cast<size_t>(idx) * f.cellW * f.cellH;
            for (int j = 0; j < f.cellH; ++j) {
                const unsigned char* row = g + j * f.cellW;
                for (int i = 0; i < f.cellW; ++i)
                    if (row[i]) blend(x + i, y + j, c, row[i]);  // la plupart des pixels d'un glyphe sont vides
            }
        }
        return x;
    }

    void textRight(const FontAtlas& f, int xRight, int y, const char* s, Color c) {
        text(f, xRight - textWidth(f, s), y, s, c);
    }

    void textCenter(const FontAtlas& f, int xCenter, int y, const char* s, Color c) {
        text(f, xCenter - textWidth(f, s) / 2, y, s, c);
    }

    // Forme lissée quelconque : `distance` donne, pour un point, sa distance signée au bord de la forme
    // (négative à l'intérieur). Seul le rectangle [x0, x1[ x [y0, y1[ est parcouru.
    template <typename Distance>
    void shape(float x0, float y0, float x1, float y1, Color c, int coverage, Distance distance) {
        const int left = x0 < 0.f ? 0 : static_cast<int>(x0), top = y0 < 0.f ? 0 : static_cast<int>(y0);
        const int right = x1 > static_cast<float>(m_w) ? m_w : static_cast<int>(std::ceil(x1));
        const int bottom = y1 > static_cast<float>(m_h) ? m_h : static_cast<int>(std::ceil(y1));
        for (int y = top; y < bottom; ++y) {
            for (int x = left; x < right; ++x) {
                float a = 0.5f - distance(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
                if (a <= 0.f) continue;
                if (a > 1.f) a = 1.f;
                blend(x, y, c, static_cast<int>(static_cast<float>(coverage) * a + 0.5f));
            }
        }
    }

    // Disque plein.
    void disc(float cx, float cy, float r, Color c, int coverage = 255) {
        shape(cx - r - 1.f, cy - r - 1.f, cx + r + 1.f, cy + r + 1.f, c, coverage,
              [=](float x, float y) { return std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy)) - r; });
    }

    // Anneau entre les rayons `inner` et `outer`.
    void ring(float cx, float cy, float inner, float outer, Color c, int coverage = 255) {
        shape(cx - outer - 1.f, cy - outer - 1.f, cx + outer + 1.f, cy + outer + 1.f, c, coverage, [=](float x, float y) {
            const float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
            return std::fmax(d - outer, inner - d);
        });
    }

    // Trait épais aux bouts arrondis, de (ax, ay) à (bx, by).
    void segment(float ax, float ay, float bx, float by, float width, Color c, int coverage = 255) {
        const float h = width * 0.5f;
        const float dx = bx - ax, dy = by - ay, length2 = dx * dx + dy * dy;
        shape(std::fmin(ax, bx) - h - 1.f, std::fmin(ay, by) - h - 1.f, std::fmax(ax, bx) + h + 1.f, std::fmax(ay, by) + h + 1.f, c, coverage,
              [=](float x, float y) {
                  float t = length2 > 0.f ? ((x - ax) * dx + (y - ay) * dy) / length2 : 0.f;
                  t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
                  const float px = ax + t * dx - x, py = ay + t * dy - y;
                  return std::sqrt(px * px + py * py) - h;
              });
    }

  private:
    uint8_t* m_px;
    int m_w, m_h, m_scale;
};

void fmt(char* out, size_t n, const char* format, float v) {
    if (std::isnan(v))
        std::snprintf(out, n, "--");
    else
        std::snprintf(out, n, format, static_cast<double>(v));
}

// Une cellule du tableau = valeur alignée à droite + unité en petite police grise, calées sur `xRight`.
// L'unité fait partie du décor fixe, la valeur est redessinée à chaque mise à jour.
void unitAt(Canvas& cv, const Theme& th, int xRight, int yTop, const char* unit) {
    // Les deux polices partagent la même ligne de base.
    cv.textRight(kFontLabel, xRight, yTop + (kFontValue.ascent - kFontLabel.ascent), unit, th.muted);
}

void valueAt(Canvas& cv, int xRight, int yTop, const char* value, const char* unit, Color color) {
    cv.textRight(kFontValue, xRight - Canvas::textWidth(kFontLabel, unit) - 5, yTop, value, color);
}

// Même cellule entièrement en petite police (présentation graphique).
void smallCell(Canvas& cv, const Theme& th, int xRight, int yTop, const char* value, const char* unit, Color color) {
    cv.textRight(kFontLabel, xRight, yTop, unit, th.muted);
    cv.textRight(kFontLabel, xRight - Canvas::textWidth(kFontLabel, unit) - 4, yTop, value, color);
}

Color frametimeColor(const Theme& th, float ms, float targetFps) {
    if (std::isnan(ms) || targetFps <= 0.f) return th.text;
    const float budget = 1000.f / targetFps;
    if (ms > budget) return th.bad;
    if (ms > 0.9f * budget) return th.warn;
    return th.text;
}

Color tempColor(const Theme& th, float t, float warn, float bad) {
    if (std::isnan(t)) return th.text;
    if (t >= bad) return th.bad;
    if (t >= warn) return th.warn;
    return th.text;
}

// Mise en page (pixels). Colonnes du tableau CPU / GPU : bord droit de chaque colonne.
const int kPad = 24;
const int kColLoad = 232;
const int kColFrame = 372;
const int kColTemp = 484;
const int kColFreq = Panel::kWidth - kPad;
const int kStatRight[3] = {Panel::kWidth - kPad - 220, Panel::kWidth - kPad - 106, Panel::kWidth - kPad};
const int kBigTop = 8;
const int kRowCpu = 124;
const int kRowGpu = 160;
const int kRowVram = 212;
const char kUnitTemp[] = "\x7f" "C";  // le code 127 de l'atlas est le signe degré
const float kCpuTempWarn = 85.f, kCpuTempBad = 95.f, kGpuTempWarn = 80.f, kGpuTempBad = 87.f;

// Présentation graphique : zone de la courbe, puis deux lignes CPU / GPU en petite police.
const int kGraphLeft = kPad;
const int kGraphWidth = PanelData::kGraphColumns * 2;
const int kGraphTop = 98;
const int kGraphBottom = 184;
const int kSmallRowCpu = 194;
const int kSmallRowGpu = 220;
const int kSmallLoad = 110, kSmallFrame = 210, kSmallTemp = 290, kSmallFreq = 400;
const int kSmallVramTitle = 446;

void hardwareRowBase(Canvas& cv, const Theme& th, int y, const char* title) {
    cv.text(kFontValue, kPad, y, title, th.accent);
    unitAt(cv, th, kColLoad, y, "%");
    unitAt(cv, th, kColFrame, y, "ms");
    unitAt(cv, th, kColTemp, y, kUnitTemp);
    unitAt(cv, th, kColFreq, y, "MHz");
}

// `estimated` : le temps par image est une estimation, affichée précédée de "~".
void hardwareRowValues(Canvas& cv, const Theme& th, int y, float load, float frameMs, float temp, float mhz, float targetFps, float tempWarn,
                       float tempBad, bool estimated) {
    char buf[32];
    fmt(buf, sizeof(buf), "%.0f", load);
    valueAt(cv, kColLoad, y, buf, "%", th.text);
    fmt(buf, sizeof(buf), estimated ? "~%.1f" : "%.1f", frameMs);
    valueAt(cv, kColFrame, y, buf, "ms", frametimeColor(th, frameMs, targetFps));
    fmt(buf, sizeof(buf), "%.0f", temp);
    valueAt(cv, kColTemp, y, buf, kUnitTemp, tempColor(th, temp, tempWarn, tempBad));
    fmt(buf, sizeof(buf), "%.0f", mhz);
    valueAt(cv, kColFreq, y, buf, "MHz", th.text);
}

// --- Bloc FPS, commun à toutes les présentations -------------------------------------------------
void drawHeader(Canvas& cv, const PanelData& d) {
    const Theme& th = d.theme;
    char buf[64];
    Color fpsColor = th.text;
    if (d.fpsValid && d.targetFps > 0.f) {
        // Vert : cadence du casque tenue. Orange : au moins la moitié (reprojection). Rouge : en dessous.
        fpsColor = d.fps >= 0.97f * d.targetFps ? th.good : (d.fps >= 0.48f * d.targetFps ? th.warn : th.bad);
    }
    if (d.fpsValid)
        std::snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(d.fps));
    else
        std::snprintf(buf, sizeof(buf), "--");
    const int labelX = cv.text(kFontBig, kPad, kBigTop, buf, fpsColor) + 8;  // à droite du grand nombre
    const int baseline = kBigTop + kFontBig.ascent;
    int x = cv.text(kFontValue, labelX, baseline - kFontValue.ascent, "FPS", th.muted);
    if (d.fpsValid && !std::isnan(d.outFps)) {
        // Avec génération d'images : cadence réellement envoyée au casque, à côté de celle du jeu.
        std::snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(d.outFps));
        x = cv.text(kFontValue, x + 16, baseline - kFontValue.ascent, buf, th.accent);
        cv.text(kFontLabel, x + 6, baseline - kFontLabel.ascent, d.english ? "with FG" : "avec FG", th.muted);
    }
    const int topLine = baseline - kFontValue.ascent - kFontLabel.cellH - 2;
    int topX = labelX;
    if (d.targetFps > 0.f) {
        std::snprintf(buf, sizeof(buf), d.english ? "target %.0f" : "cible %.0f", static_cast<double>(d.targetFps));
        topX = cv.text(kFontLabel, labelX, topLine, buf, th.muted) + 18;
    }
    // Enregistrement d'un rapport en mode manuel : point et durée, puis l'issue quelques secondes.
    if (d.recordState >= 1 && d.recordState <= 3) {
        const Color dot = d.recordState == 1 ? th.bad : (d.recordState == 2 ? th.good : th.warn);
        const float r = 5.f;
        cv.disc(static_cast<float>(topX) + r, static_cast<float>(topLine) + kFontLabel.cellH * 0.5f, r, dot);
        const int seconds = d.recordSeconds > 0.0 ? static_cast<int>(d.recordSeconds) : 0;
        if (d.recordState == 3)
            std::snprintf(buf, sizeof(buf), "< 5 s");
        else if (seconds < 3600)
            std::snprintf(buf, sizeof(buf), d.recordState == 1 ? "REC %02d:%02d" : "OK %02d:%02d", seconds / 60, seconds % 60);
        else
            std::snprintf(buf, sizeof(buf), d.recordState == 1 ? "REC %dh%02d" : "OK %dh%02d", seconds / 3600, (seconds / 60) % 60);
        cv.text(kFontLabel, topX + static_cast<int>(2 * r) + 6, topLine, buf, d.recordState == 1 ? th.text : dot);
    }

    // AVG / MIN / MAX
    const float statValue[3] = {d.fpsAvg, d.fpsMin, d.fpsMax};
    for (int i = 0; i < 3; ++i) {
        if (d.fpsValid)
            std::snprintf(buf, sizeof(buf), i == 0 ? "%.1f" : "%.0f", static_cast<double>(statValue[i]));
        else
            std::snprintf(buf, sizeof(buf), "--");
        cv.textRight(kFontValue, kStatRight[i], 40, buf, th.text);
    }
}

// --- Présentation complète : tableau CPU / GPU, heure, VRAM --------------------------------------
void drawClassicBody(Canvas& cv, const PanelData& d) {
    const Theme& th = d.theme;
    char buf[64];
    hardwareRowValues(cv, th, kRowCpu, d.cpuLoad, d.cpuFrameMs, d.cpuTemp, d.cpuMHz, d.targetFps, kCpuTempWarn, kCpuTempBad, false);
    hardwareRowValues(cv, th, kRowGpu, d.gpuLoad, d.gpuFrameMs, d.gpuTemp, d.gpuMHz, d.targetFps, kGpuTempWarn, kGpuTempBad, !d.gpuMeasured);

    // Heure, en bas à droite.
    int rightLimit = Panel::kWidth - kPad;  // bord droit disponible pour la ligne VRAM
    if (d.clockMinutes >= 0) {
        std::snprintf(buf, sizeof(buf), "%02d:%02d", (d.clockMinutes / 60) % 24, d.clockMinutes % 60);
        cv.textRight(kFontValue, Panel::kWidth - kPad, kRowVram, buf, th.text);
        rightLimit -= Canvas::textWidth(kFontValue, buf) + 18;
    }

    // Ligne VRAM (ou message si les capteurs ne répondent pas).
    if (!d.sensorsOk) {
        cv.text(kFontLabel, kPad, kRowVram + 4, d.english ? "No sensors: start XRPerfSensors.exe" : "Capteurs absents : lancer XRPerfSensors.exe", th.warn);
        return;
    }
    int x = cv.text(kFontValue, kPad, kRowVram, "VRAM", th.accent);
    const bool vramOk = !std::isnan(d.vramUsedMB) && !std::isnan(d.vramTotalMB) && d.vramTotalMB > 0.f;
    fmt(buf, sizeof(buf), "%.1f", d.vramUsedMB / 1024.f);
    const int usedRight = x + 14 + 5 * kFontValue.cellW;  // place pour "99.9" + marge
    cv.textRight(kFontValue, usedRight, kRowVram, buf, th.text);
    if (vramOk)
        std::snprintf(buf, sizeof(buf), d.english ? "/ %.1f GB" : "/ %.1f Go", static_cast<double>(d.vramTotalMB / 1024.f));
    else
        std::snprintf(buf, sizeof(buf), d.english ? "GB" : "Go");
    x = cv.text(kFontLabel, usedRight + 8, kRowVram + (kFontValue.ascent - kFontLabel.ascent), buf, th.muted);

    if (vramOk) {
        // Jauge d'occupation.
        float ratio = d.vramUsedMB / d.vramTotalMB;
        if (ratio < 0.f) ratio = 0.f;
        if (ratio > 1.f) ratio = 1.f;
        const int barX = x + 18, barW = rightLimit - barX, barH = 10, barY = kRowVram + 10;
        if (barW > 20) {
            cv.fillRoundRect(barX, barY, barW, barH, 5.f, th.line, 255);
            const Color fill = ratio >= 0.97f ? th.bad : (ratio >= 0.90f ? th.warn : th.accent);
            const int fillW = static_cast<int>(static_cast<float>(barW) * ratio + 0.5f);
            if (fillW >= 10) cv.fillRoundRect(barX, barY, fillW, barH, 5.f, fill, 255);
        }
    }
}

// --- Présentation graphique : courbe du temps d'image, puis CPU / GPU sur deux lignes -------------
void drawGraph(Canvas& cv, const PanelData& d) {
    const Theme& th = d.theme;
    const int height = kGraphBottom - kGraphTop;
    const float budget = d.targetFps > 0.f ? 1000.f / d.targetFps : std::numeric_limits<float>::quiet_NaN();

    // Échelle : le budget d'une image (durée à ne pas dépasser pour tenir la cible) est à mi-hauteur.
    // Sans cible connue, l'échelle suit la plus grande valeur visible.
    float top = 2.f * budget;
    if (std::isnan(budget)) {
        top = 20.f;
        for (int i = 0; d.graphValid && i < PanelData::kGraphColumns; ++i)
            if (d.graphFrameMs[i] * 1.25f > top) top = d.graphFrameMs[i] * 1.25f;
    }
    const auto toY = [&](float ms) {
        float ratio = ms / top;
        if (!(ratio > 0.f)) ratio = 0.f;  // négatif ou NaN
        if (ratio > 1.f) ratio = 1.f;
        return kGraphBottom - static_cast<int>(ratio * static_cast<float>(height) + 0.5f);
    };

    if (d.graphValid) {
        // Temps d'image : surface légère sous la courbe, trait plein au-dessus ; rouge quand le budget est dépassé.
        int previous = -1;
        for (int i = 0; i < PanelData::kGraphColumns; ++i) {
            const float ms = d.graphFrameMs[i];
            if (!(ms > 0.f)) {
                previous = -1;  // pas d'image dans cette tranche (pause, chargement) : la courbe s'interrompt
                continue;
            }
            const int x = kGraphLeft + i * 2;
            const int y = toY(ms);
            const Color color = (!std::isnan(budget) && ms > budget * 1.03f) ? th.bad : th.accent;
            cv.fillRect(x, y, 2, kGraphBottom - y, color, 56);
            cv.fillRect(x, y - 1, 2, 2, color);
            if (previous >= 0 && previous != y) {
                const int from = previous < y ? previous : y;
                const int to = previous < y ? y : previous;
                cv.fillRect(x, from - 1, 1, to - from + 2, color);
            }
            previous = y;
        }
        // Temps CPU et GPU : traits fins, dans la couleur du texte et dans celle de la courbe GPU.
        const auto thinLine = [&](const float* values, Color color, int coverage) {
            int last = -1;
            for (int i = 0; i < PanelData::kGraphColumns; ++i) {
                const float ms = values[i];
                if (!(ms > 0.f)) {
                    last = -1;
                    continue;
                }
                const int x = kGraphLeft + i * 2;
                const int y = toY(ms);
                cv.fillRect(x, y, 2, 1, color, coverage);
                if (last >= 0 && last != y) {
                    const int from = last < y ? last : y;
                    cv.fillRect(x, from, 1, (last < y ? y : last) - from + 1, color, coverage);
                }
                last = y;
            }
        };
        thinLine(d.graphGpuMs, th.gpu, 235);
        thinLine(d.graphCpuMs, th.text, 215);
    }

    if (!std::isnan(budget)) {
        // Budget d'une image : pointillés et valeur, au-dessus de la courbe pour rester lisibles.
        const int y = toY(budget);
        for (int x = kGraphLeft; x < kGraphLeft + kGraphWidth; x += 8) cv.fillRect(x, y, 4, 1, th.muted);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f ms", static_cast<double>(budget));
        cv.textRight(kFontLabel, kGraphLeft + kGraphWidth, y - kFontLabel.cellH, buf, th.muted);
    }
    // Légende, en haut à gauche de la courbe.
    int legend = cv.text(kFontLabel, kGraphLeft, kGraphTop - 2, d.english ? "frame time" : "temps d'image", th.accent);
    legend = cv.text(kFontLabel, legend + 2 * kFontLabel.cellW, kGraphTop - 2, "CPU", th.text);
    cv.text(kFontLabel, legend + 2 * kFontLabel.cellW, kGraphTop - 2, d.gpuMeasured ? "GPU" : "GPU (est.)", th.gpu);
}

void smallRow(Canvas& cv, const Theme& th, int y, const char* title, float load, float frameMs, float temp, float mhz, float targetFps,
              float tempWarn, float tempBad, bool sensorsOk, bool estimated) {
    char buf[32];
    cv.text(kFontLabel, kPad, y, title, th.accent);
    fmt(buf, sizeof(buf), estimated ? "~%.1f" : "%.1f", frameMs);
    smallCell(cv, th, kSmallFrame, y, buf, "ms", frametimeColor(th, frameMs, targetFps));
    if (!sensorsOk) return;  // les autres colonnes viennent du programme de capteurs
    fmt(buf, sizeof(buf), "%.0f", load);
    smallCell(cv, th, kSmallLoad, y, buf, "%", th.text);
    fmt(buf, sizeof(buf), "%.0f", temp);
    smallCell(cv, th, kSmallTemp, y, buf, kUnitTemp, tempColor(th, temp, tempWarn, tempBad));
    fmt(buf, sizeof(buf), "%.0f", mhz);
    smallCell(cv, th, kSmallFreq, y, buf, "MHz", th.text);
}

void drawGraphBody(Canvas& cv, const PanelData& d) {
    const Theme& th = d.theme;
    char buf[64];
    drawGraph(cv, d);

    smallRow(cv, th, kSmallRowCpu, "CPU", d.cpuLoad, d.cpuFrameMs, d.cpuTemp, d.cpuMHz, d.targetFps, kCpuTempWarn, kCpuTempBad, d.sensorsOk, false);
    if (d.clockMinutes >= 0) {
        std::snprintf(buf, sizeof(buf), "%02d:%02d", (d.clockMinutes / 60) % 24, d.clockMinutes % 60);
        cv.textRight(kFontLabel, Panel::kWidth - kPad, kSmallRowCpu, buf, th.text);
    }
    if (!d.sensorsOk) {
        // Sans capteurs, seul le temps GPU mesuré (option) reste connu : il garde sa place, le message
        // raccourci se met à sa droite.
        if (d.gpuMeasured) {
            smallRow(cv, th, kSmallRowGpu, "GPU", d.gpuLoad, d.gpuFrameMs, d.gpuTemp, d.gpuMHz, d.targetFps, kGpuTempWarn, kGpuTempBad, false, false);
            cv.text(kFontLabel, kSmallFrame + 3 * kFontLabel.cellW, kSmallRowGpu, d.english ? "No sensors (XRPerfSensors)" : "Capteurs absents (XRPerfSensors)", th.warn);
        } else {
            cv.text(kFontLabel, kPad, kSmallRowGpu, d.english ? "No sensors: start XRPerfSensors.exe" : "Capteurs absents : lancer XRPerfSensors.exe", th.warn);
        }
        return;
    }
    smallRow(cv, th, kSmallRowGpu, "GPU", d.gpuLoad, d.gpuFrameMs, d.gpuTemp, d.gpuMHz, d.targetFps, kGpuTempWarn, kGpuTempBad, true, !d.gpuMeasured);
    cv.text(kFontLabel, kSmallVramTitle, kSmallRowGpu, "VRAM", th.accent);
    if (!std::isnan(d.vramUsedMB) && !std::isnan(d.vramTotalMB) && d.vramTotalMB > 0.f)
        std::snprintf(buf, sizeof(buf), "%.1f/%.1f", static_cast<double>(d.vramUsedMB / 1024.f), static_cast<double>(d.vramTotalMB / 1024.f));
    else
        fmt(buf, sizeof(buf), "%.1f", d.vramUsedMB / 1024.f);
    smallCell(cv, th, Panel::kWidth - kPad, kSmallRowGpu, buf, d.english ? "GB" : "Go", th.text);
}

// --- Casque et résolution de rendu, sous le reste -------------------------------------------------
void drawInfo(Canvas& cv, const PanelData& d, int y) {
    const Theme& th = d.theme;
    char buf[64];
    const char* suffix = d.english ? " per eye" : " par oeil";  // l'atlas ne contient que l'ASCII
    int right = Panel::kWidth - kPad;
    cv.textRight(kFontLabel, right, y, suffix, th.muted);
    right -= Canvas::textWidth(kFontLabel, suffix);
    if (d.renderWidth > 0 && d.renderHeight > 0 && d.renderWidth < 100000 && d.renderHeight < 100000) {
        // Écart avec la résolution recommandée par le runtime (échelle de rendu propre au jeu) : affiché
        // seulement s'il est net, en facteur de la largeur (x1.20 = 20 % de plus dans chaque sens).
        const float scale = d.recommendedWidth > 0 && d.recommendedWidth < 100000 ? static_cast<float>(d.renderWidth) / d.recommendedWidth : 1.f;
        if (scale >= 1.03f || scale <= 0.97f)
            std::snprintf(buf, sizeof(buf), "%d x %d (x%.2f)", d.renderWidth, d.renderHeight, static_cast<double>(scale));
        else
            std::snprintf(buf, sizeof(buf), "%d x %d", d.renderWidth, d.renderHeight);
    } else if (d.recommendedWidth > 0 && d.recommendedHeight > 0 && d.recommendedWidth < 100000 && d.recommendedHeight < 100000) {
        // Jeu SteamVR (OpenVR) : la taille de l'image du jeu n'est pas visible, seule celle recommandée l'est.
        std::snprintf(buf, sizeof(buf), "%d x %d (recomm.)", d.recommendedWidth, d.recommendedHeight);
    } else
        std::snprintf(buf, sizeof(buf), "-- x --");
    cv.textRight(kFontLabel, right, y, buf, th.text);
    right -= Canvas::textWidth(kFontLabel, buf) + 2 * kFontLabel.cellW;

    // Le nom occupe la place restante à gauche ; trop long, il est coupé et terminé par "..".
    const size_t room = static_cast<size_t>((right - kPad) / kFontLabel.cellW);
    size_t length = 0;
    while (length < sizeof(d.headset) && d.headset[length]) ++length;
    if (length == 0) {
        cv.text(kFontLabel, kPad, y, d.english ? "Unknown headset" : "Casque inconnu", th.muted);
    } else {
        char name[sizeof(d.headset) + 1];
        const size_t shown = length <= room ? length : (room > 2 ? room - 2 : 0);
        std::memcpy(name, d.headset, shown);
        name[shown] = '\0';
        const int end = cv.text(kFontLabel, kPad, y, name, th.accent);
        if (shown < length) cv.text(kFontLabel, end, y, "..", th.muted);
    }
}

}  // namespace

// --- Montre ------------------------------------------------------------------------------------

namespace {

const float kPi = 3.14159265358979f;

// Couleur des FPS selon la cadence demandée (même règle que le chiffre du panneau).
Color fpsColor(const Theme& th, float fps, float target) {
    if (target <= 0.f) return th.text;
    if (fps >= 0.97f * target) return th.good;
    if (fps >= 0.5f * target) return th.warn;
    return th.bad;
}

// Durée de session : "12:34" sous une heure, "1:23:45" au-delà.
void sessionText(char* out, size_t n, double seconds) {
    const long total = static_cast<long>(seconds < 0.0 ? 0.0 : seconds);
    const long h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    if (h > 0)
        std::snprintf(out, n, "%ld:%02ld:%02ld", h, m, s);
    else
        std::snprintf(out, n, "%ld:%02ld", m, s);
}

bool batteryKnown(const WatchData& d, int hand) { return !std::isnan(d.battery[hand]); }

bool showBattery(const WatchData& d) { return (d.elements & kWatchBattery) && (batteryKnown(d, 0) || batteryKnown(d, 1)); }

Color batteryColor(const Theme& th, float level) {
    if (level < 0.2f) return th.bad;
    if (level < 0.4f) return th.warn;
    return th.good;
}

// Pile horizontale : contour, remplissage selon le niveau, plot à droite ; lettre de la main à gauche,
// pourcentage à droite. (x, y) = coin haut-gauche de la lettre. Retourne le x de fin.
int batteryGauge(Canvas& cv, const Theme& th, int x, int y, const char* hand, float level, bool charging) {
    x = cv.text(kFontLabel, x, y, hand, th.muted) + 6;
    const int w = 40, h = 18, top = y + 2;
    cv.fillRoundRect(x, top, w, h, 4.f, th.muted, 255);
    cv.fillRoundRect(x + 2, top + 2, w - 4, h - 4, 3.f, th.background, 255);
    cv.fillRect(x + w, top + 5, 3, h - 10, th.muted);
    if (!std::isnan(level)) {
        const float clamped = level < 0.f ? 0.f : (level > 1.f ? 1.f : level);
        const int fill = static_cast<int>((w - 8) * clamped + 0.5f);
        if (fill > 0) cv.fillRect(x + 4, top + 4, fill, h - 8, charging ? th.accent : batteryColor(th, clamped));
    }
    char buf[16];
    if (std::isnan(level))
        std::snprintf(buf, sizeof(buf), "--");
    else
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(level * 100.f + 0.5f));
    return cv.text(kFontLabel, x + w + 8, y, buf, std::isnan(level) ? th.muted : th.text);
}

// Les deux piles côte à côte, centrées sur `cx`.
void batteryPair(Canvas& cv, const WatchData& d, int cx, int y) {
    const Theme& th = d.theme;
    const int gaugeWidth = kFontLabel.cellW + 6 + 43 + 8 + 4 * kFontLabel.cellW;  // lettre, pile, "100%"
    const int gap = 14, left = cx - (2 * gaugeWidth + gap) / 2;
    batteryGauge(cv, th, left, y, d.english ? "L" : "G", d.battery[0], d.charging[0]);
    batteryGauge(cv, th, left + gaugeWidth + gap, y, d.english ? "R" : "D", d.battery[1], d.charging[1]);
}

// "72 FPS" centré : chiffre en couleur de cadence, unité en gris.
void fpsLine(Canvas& cv, const WatchData& d, int cx, int y, const FontAtlas& font) {
    const Theme& th = d.theme;
    char buf[16];
    if (d.fpsValid)
        std::snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(d.fps));
    else
        std::snprintf(buf, sizeof(buf), "--");
    const int valueWidth = Canvas::textWidth(font, buf), unitWidth = Canvas::textWidth(kFontLabel, "FPS");
    const int left = cx - (valueWidth + 6 + unitWidth) / 2;
    cv.text(font, left, y, buf, d.fpsValid ? fpsColor(th, d.fps, d.targetFps) : th.muted);
    cv.text(kFontLabel, left + valueWidth + 6, y + (font.ascent - kFontLabel.ascent), "FPS", th.muted);
}

// Heure en grands chiffres "HH:MM" centrée ; le ":" (absent de la grande police) est dessiné.
void bigTime(Canvas& cv, const WatchData& d, int cx, int y) {
    const Theme& th = d.theme;
    char hh[4], mm[4];
    if (d.hour < 0) {
        std::snprintf(hh, sizeof(hh), "--");
        std::snprintf(mm, sizeof(mm), "--");
    } else {
        std::snprintf(hh, sizeof(hh), "%02d", d.hour % 24);
        std::snprintf(mm, sizeof(mm), "%02d", d.minute % 60);
    }
    const int digitsWidth = Canvas::textWidth(kFontBig, hh), colon = 20;
    const int left = cx - (2 * digitsWidth + colon) / 2;
    cv.text(kFontBig, left, y, hh, th.text);
    const float colonX = static_cast<float>(left + digitsWidth) + colon * 0.5f;
    const bool blink = d.hour >= 0 && (d.second % 2) == 1;  // deux points qui clignotent chaque seconde
    const Color colonColor = blink ? th.muted : th.text;
    cv.disc(colonX, static_cast<float>(y + kFontBig.ascent) - 34.f, 4.f, colonColor);
    cv.disc(colonX, static_cast<float>(y + kFontBig.ascent) - 12.f, 4.f, colonColor);
    cv.text(kFontBig, left + digitsWidth + colon, y, mm, th.text);
}

// --- Ligne session / batterie du panneau (option) ---------------------------------------------
void drawStatus(Canvas& cv, const PanelData& d, int y) {
    const Theme& th = d.theme;
    char buf[32];
    if (d.statusElements & kWatchSession) {
        // Intitulé « SESSION » dans le décor fixe ; la durée à sa suite.
        if (d.sessionSeconds >= 0.0)
            sessionText(buf, sizeof(buf), d.sessionSeconds);
        else
            std::snprintf(buf, sizeof(buf), "--");
        cv.text(kFontLabel, kPad + Canvas::textWidth(kFontLabel, "SESSION") + kFontLabel.cellW, y, buf, th.text);
    }
    if (d.statusElements & kWatchBattery) {
        const bool known = !std::isnan(d.battery[0]) || !std::isnan(d.battery[1]);
        if (!known) {
            // Runtime qui ne donne pas la batterie (Pimax OpenXR...) : dit simplement, à droite.
            cv.textRight(kFontLabel, Panel::kWidth - kPad, y, d.english ? "battery unavailable" : "batterie indisponible", th.muted);
            return;
        }
        // Deux piles alignées à droite, comme sur la montre.
        const int gaugeWidth = kFontLabel.cellW + 6 + 43 + 8 + 4 * kFontLabel.cellW;
        const int gap = 18, left = Panel::kWidth - kPad - (2 * gaugeWidth + gap);
        batteryGauge(cv, th, left, y, d.english ? "L" : "G", d.battery[0], d.charging[0]);
        batteryGauge(cv, th, left + gaugeWidth + gap, y, d.english ? "R" : "D", d.battery[1], d.charging[1]);
    }
}

void drawClassicWatch(Canvas& cv, const WatchData& d) {
    const Theme& th = d.theme;
    const float c = WatchFace::kSize * 0.5f;
    cv.disc(c, c, 158.f, mix(th.line, th.accent, 90));     // lunette
    cv.disc(c, c, 148.f, th.background);                   // cadran
    cv.ring(c, c, 146.f, 148.f, mix(th.background, th.text, 40));
    for (int i = 0; i < 60; ++i) {
        const float a = static_cast<float>(i) * kPi / 30.f, sx = std::sin(a), cy = -std::cos(a);
        const bool hourMark = i % 5 == 0, cardinal = i % 15 == 0;
        const float outer = 140.f, inner = cardinal ? 116.f : (hourMark ? 122.f : 134.f);
        cv.segment(c + sx * inner, c + cy * inner, c + sx * outer, c + cy * outer, cardinal ? 6.f : (hourMark ? 4.f : 1.5f),
                   hourMark ? th.text : th.muted);
    }
    char buf[24];
    if (d.elements & kWatchFps) fpsLine(cv, d, static_cast<int>(c), 62, kFontValue);
    if (d.elements & kWatchSession && d.sessionSeconds >= 0.0) {
        sessionText(buf, sizeof(buf), d.sessionSeconds);
        cv.textCenter(kFontValue, static_cast<int>(c), 214, buf, th.text);
        cv.textCenter(kFontLabel, static_cast<int>(c), 244, "SESSION", th.muted);
    }
    if (showBattery(d)) {
        // Gauche vers 9 h, droite vers 3 h.
        for (int hand = 0; hand < 2; ++hand) {
            const int cx = hand == 0 ? 82 : 238;
            const float level = d.battery[hand];
            if (std::isnan(level))
                std::snprintf(buf, sizeof(buf), "--");
            else
                std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(level * 100.f + 0.5f));
            cv.textCenter(kFontLabel, cx, 140, hand == 0 ? (d.english ? "L" : "G") : (d.english ? "R" : "D"), th.muted);
            cv.textCenter(kFontLabel, cx, 162, buf, std::isnan(level) ? th.muted : (d.charging[hand] ? th.accent : batteryColor(th, level)));
        }
    }
    if (d.hour >= 0) {
        const float seconds = static_cast<float>(d.second);
        const float minutes = static_cast<float>(d.minute) + seconds / 60.f;
        const float hours = static_cast<float>(d.hour % 12) + minutes / 60.f;
        const auto hand = [&](float turns, float length, float tail, float width, Color color) {
            const float a = turns * 2.f * kPi, sx = std::sin(a), cy = -std::cos(a);
            cv.segment(c - sx * tail, c - cy * tail, c + sx * length, c + cy * length, width, color);
        };
        hand(hours / 12.f, 74.f, 14.f, 9.f, th.text);
        hand(minutes / 60.f, 114.f, 16.f, 6.f, th.text);
        hand(seconds / 60.f, 128.f, 24.f, 2.f, th.bad);
    }
    cv.disc(c, c, 8.f, th.bad);
    cv.disc(c, c, 3.f, th.background);
}

void drawDigitalWatch(Canvas& cv, const WatchData& d) {
    const Theme& th = d.theme;
    const int s = WatchFace::kSize;
    cv.fillRoundRect(0, 0, s, s, 56.f, mix(th.line, th.accent, 90), 255);
    cv.fillRoundRect(8, 8, s - 16, s - 16, 50.f, th.background, 255);
    char buf[24];
    if (d.elements & kWatchFps) fpsLine(cv, d, s / 2, 34, kFontValue);
    bigTime(cv, d, s / 2, 82);
    if (d.hour >= 0) {
        std::snprintf(buf, sizeof(buf), "%02d", d.second % 60);
        cv.textCenter(kFontLabel, s / 2, 154, buf, th.muted);
    }
    const bool session = (d.elements & kWatchSession) && d.sessionSeconds >= 0.0;
    if (session || showBattery(d)) cv.fillRect(40, 186, s - 80, 1, th.line);
    if (session) {
        sessionText(buf, sizeof(buf), d.sessionSeconds);
        const int labelWidth = Canvas::textWidth(kFontLabel, "SESSION"), valueWidth = Canvas::textWidth(kFontValue, buf);
        const int left = s / 2 - (labelWidth + 10 + valueWidth) / 2;
        cv.text(kFontLabel, left, 204 + (kFontValue.ascent - kFontLabel.ascent), "SESSION", th.muted);
        cv.text(kFontValue, left + labelWidth + 10, 204, buf, th.text);
    }
    if (showBattery(d)) batteryPair(cv, d, s / 2, 250);
}

void drawSportWatch(Canvas& cv, const WatchData& d) {
    const Theme& th = d.theme;
    const float c = WatchFace::kSize * 0.5f;
    cv.disc(c, c, 158.f, th.background);
    // Couronne de 60 graduations : celles des secondes écoulées s'allument.
    for (int i = 0; i < 60; ++i) {
        const float a = static_cast<float>(i) * kPi / 30.f, sx = std::sin(a), cy = -std::cos(a);
        const bool lit = d.hour >= 0 && i <= d.second % 60;
        const float inner = i % 5 == 0 ? 136.f : 142.f;
        cv.segment(c + sx * inner, c + cy * inner, c + sx * 152.f, c + cy * 152.f, i % 5 == 0 ? 4.f : 3.f, lit ? th.accent : th.line);
    }
    char buf[24];
    if (d.elements & kWatchFps) fpsLine(cv, d, static_cast<int>(c), 58, kFontValue);
    bigTime(cv, d, static_cast<int>(c), 98);
    if ((d.elements & kWatchSession) && d.sessionSeconds >= 0.0) {
        sessionText(buf, sizeof(buf), d.sessionSeconds);
        cv.textCenter(kFontValue, static_cast<int>(c), 178, buf, th.text);
    }
    if (showBattery(d)) batteryPair(cv, d, static_cast<int>(c), 222);
}

}  // namespace

void WatchFace::render(const WatchData& d, float opacity) {
    if (opacity < 0.f) opacity = 0.f;
    if (opacity > 1.f) opacity = 1.f;
    std::fill(m_px.begin(), m_px.end(), static_cast<uint8_t>(0));
    Canvas cv(m_px.data(), kSize, kSize, static_cast<int>(opacity * 256.f + 0.5f));
    switch (d.style) {
        case WatchStyle::Digital: drawDigitalWatch(cv, d); break;
        case WatchStyle::Sport: drawSportWatch(cv, d); break;
        case WatchStyle::Classic:
        default: drawClassicWatch(cv, d); break;
    }
}

// Décor fixe : fond, intitulés, unités, séparateurs.
void Panel::buildBase(const PanelData& d, int opacity, int scale) {
    const Theme& th = d.theme;
    const int body = bodyHeight(d.layout);
    m_base.assign(m_px.size(), 0);
    Canvas cv(m_base.data(), kWidth, kHeight, scale);
    cv.fillRoundRect(0, 0, kWidth, contentHeight(d), 18.f, th.background, opacity);

    const char* statName[3] = {"AVG", "MIN", "MAX"};
    for (int i = 0; i < 3; ++i) cv.textRight(kFontLabel, kStatRight[i], 16, statName[i], th.muted);

    if (d.layout != Layout::Compact) cv.fillRect(kPad, 88, kWidth - 2 * kPad, 1, th.line);
    if (d.layout == Layout::Classic) {
        const int headTop = 98;
        cv.textRight(kFontLabel, kColLoad, headTop, d.english ? "LOAD" : "CHARGE", th.muted);
        cv.textRight(kFontLabel, kColFrame, headTop, "FRAMETIME", th.muted);
        cv.textRight(kFontLabel, kColTemp, headTop, "TEMP.", th.muted);
        cv.textRight(kFontLabel, kColFreq, headTop, d.english ? "CLOCK" : "FREQ.", th.muted);
        hardwareRowBase(cv, th, kRowCpu, "CPU");
        hardwareRowBase(cv, th, kRowGpu, "GPU");
        cv.fillRect(kPad, 200, kWidth - 2 * kPad, 1, th.line);
    } else if (d.layout == Layout::Graph) {
        cv.fillRect(kGraphLeft, kGraphBottom, kGraphWidth, 1, th.line);  // axe du temps
    }
    // Séparateurs au-dessus des lignes du bas : session / batterie, puis casque / résolution.
    const bool status = d.statusElements != 0;
    if (status) {
        cv.fillRect(kPad, body - 8, kWidth - 2 * kPad, 1, th.line);
        if (d.statusElements & kWatchSession) cv.text(kFontLabel, kPad, body + 4, "SESSION", th.muted);
    }
    if (d.showInfo) cv.fillRect(kPad, body + (status ? kStatusHeight : 0) - 8, kWidth - 2 * kPad, 1, th.line);

    m_baseOpacity = opacity;
    m_baseScale = scale;
    m_baseInfo = d.showInfo;
    m_baseStatus = d.statusElements;
    m_baseLayout = d.layout;
    m_baseTheme = th;
    m_baseEnglish = d.english;
}

void Panel::render(const PanelData& d, float bgOpacity, float globalOpacity) {
    if (bgOpacity < 0.f) bgOpacity = 0.f;
    if (bgOpacity > 1.f) bgOpacity = 1.f;
    const int opacity = static_cast<int>(bgOpacity * 255.f + 0.5f);
    if (globalOpacity < 0.f) globalOpacity = 0.f;
    if (globalOpacity > 1.f) globalOpacity = 1.f;
    const int scale = static_cast<int>(globalOpacity * 256.f + 0.5f);
    if (m_base.size() != m_px.size() || opacity != m_baseOpacity || scale != m_baseScale || d.showInfo != m_baseInfo ||
        d.statusElements != m_baseStatus || d.layout != m_baseLayout || d.english != m_baseEnglish || std::memcmp(&d.theme, &m_baseTheme, sizeof(Theme)) != 0)
        buildBase(d, opacity, scale);
    std::memcpy(m_px.data(), m_base.data(), m_px.size());

    Canvas cv(m_px.data(), kWidth, kHeight, scale);
    drawHeader(cv, d);
    if (d.layout == Layout::Classic)
        drawClassicBody(cv, d);
    else if (d.layout == Layout::Graph)
        drawGraphBody(cv, d);
    const int body = bodyHeight(d.layout);
    if (d.statusElements != 0) drawStatus(cv, d, body + 4);
    if (d.showInfo) drawInfo(cv, d, body + (d.statusElements != 0 ? kStatusHeight : 0) + 4);
}

}  // namespace xrpo
