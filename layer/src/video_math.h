// Calculs de l'enregistrement vidéo qui ne touchent ni à Direct3D ni à Windows : choix des images à
// copier, cadrage dans l'image d'un œil, son mis bout à bout. Code portable, testé dans tests/preview.cpp.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>

#include "pose_math.h"

namespace xrpo {

// --- Images à copier --------------------------------------------------------------------------------
//
// Le jeu rend à la cadence du casque (72, 90, 120 Hz...), la vidéo en veut moins (30 ou 60). Plutôt que
// de copier chaque image, on vise des instants réguliers et on prend l'image la plus proche de chacun.
// Si le jeu rend à peine plus vite que la vidéo (ou moins vite), chaque image est copiée.
class FrameThrottle {
  public:
    void reset() {
        m_next = -1.0;
        m_last = -1.0;
        m_interval = 0.0;
    }

    // Image rendue par le jeu à l'instant `now` (secondes) : faut-il la copier ?
    bool take(double now, double fps) {
        if (m_last >= 0.0) {
            const double dt = now - m_last;
            if (dt > 0.0 && dt < 0.5) m_interval = m_interval > 0.0 ? m_interval * 0.9 + dt * 0.1 : dt;
        }
        m_last = now;
        if (!(fps > 0.0)) return true;
        const double period = 1.0 / fps;
        // Jeu à peine plus rapide que la vidéo (ou plus lent) : chaque image.
        if (m_interval <= 0.0 || m_interval > period * 0.95) return true;
        if (m_next < 0.0 || now - m_next > period) m_next = now;  // début, ou retard d'une image entière : on se recale
        // L'image la plus proche de chaque instant prévu : au plus une demi-image d'avance.
        if (now + std::min(m_interval, period) * 0.5 >= m_next) {
            m_next += period;
            return true;
        }
        return false;
    }

  private:
    double m_next = -1.0;
    double m_last = -1.0;
    double m_interval = 0.0;  // durée moyenne d'une image du jeu
};

// --- Direction « droit devant » ----------------------------------------------------------------------
//
// Sur un casque aux écrans inclinés (Pimax), chaque œil regarde un peu vers l'extérieur : le centre de
// son image n'est pas devant la tête. La direction de la tête est entre les deux yeux ; on la
// ramène dans le repère de l'œil enregistré, en tangentes d'angle (comme les bords de l'image).
// Orientation entre deux autres (le chemin le plus court), normalisée.
inline Quat MidOrientation(const Quat& a, const Quat& b, float t = 0.5f) {
    Quat r = b;
    if (a.x * r.x + a.y * r.y + a.z * r.z + a.w * r.w < 0.f) r = {-r.x, -r.y, -r.z, -r.w};
    Quat m = {a.x + (r.x - a.x) * t, a.y + (r.y - a.y) * t, a.z + (r.z - a.z) * t, a.w + (r.w - a.w) * t};
    const float n = std::sqrt(m.x * m.x + m.y * m.y + m.z * m.z + m.w * m.w);
    if (!(n > 1e-6f)) return a;
    return {m.x / n, m.y / n, m.z / n, m.w / n};
}

inline Quat Conjugate(const Quat& q) { return {-q.x, -q.y, -q.z, q.w}; }

inline Quat Normalized(const Quat& q) {
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(n > 1e-6f)) return Quat();
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}

inline void HeadForwardInEye(const Quat& eye, const Quat& left, const Quat& right, float& tanX, float& tanY) {
    tanX = 0.f;
    tanY = 0.f;
    Quat r = right;
    if (left.x * r.x + left.y * r.y + left.z * r.z + left.w * r.w < 0.f) r = {-r.x, -r.y, -r.z, -r.w};
    const float n = std::sqrt((left.x + r.x) * (left.x + r.x) + (left.y + r.y) * (left.y + r.y) + (left.z + r.z) * (left.z + r.z) +
                              (left.w + r.w) * (left.w + r.w));
    if (!(n > 1e-6f)) return;
    const Quat mid = MidOrientation(left, right);
    const Vec3 forward = Rotate(Conjugate(eye), Rotate(mid, {0.f, 0.f, -1.f}));
    if (!(forward.z < -0.1f)) return;  // invraisemblable : on garde le centre de l'œil
    tanX = forward.x / -forward.z;
    tanY = forward.y / -forward.z;
}

// --- Cadrage -----------------------------------------------------------------------------------------
//
// Rectangle de la vidéo dans l'image de l'œil (coordonnées 0 à 1, origine en haut à gauche). `aspect` :
// largeur / hauteur de la vidéo ; `field` : part de l'image gardée (1 = le plus grand rectangle qui
// tient dans l'image) ; le rectangle est centré sur la direction de la tête, décalé si besoin pour
// rester dans l'image.
struct CropBox {
    float x = 0.f, y = 0.f, w = 1.f, h = 1.f;
};

inline CropBox ComputeCrop(int width, int height, float tanLeft, float tanRight, float tanUp, float tanDown, float centerX, float centerY,
                           float aspect, float field) {
    CropBox box;
    if (width <= 0 || height <= 0 || !(aspect > 0.f)) return box;
    field = std::min(1.f, std::max(0.2f, std::isfinite(field) ? field : 1.f));
    const float W = static_cast<float>(width), H = static_cast<float>(height);
    float u0 = 0.5f, v0 = 0.5f;
    if (tanRight - tanLeft > 1e-4f && tanUp - tanDown > 1e-4f && std::isfinite(centerX) && std::isfinite(centerY)) {
        u0 = (centerX - tanLeft) / (tanRight - tanLeft);
        v0 = (tanUp - centerY) / (tanUp - tanDown);
        u0 = std::min(1.f, std::max(0.f, u0));
        v0 = std::min(1.f, std::max(0.f, v0));
    }
    float w = std::min(W, H * aspect) * field;  // en pixels
    float h = w / aspect;
    const float cx = std::min(W - w * 0.5f, std::max(w * 0.5f, u0 * W));
    const float cy = std::min(H - h * 0.5f, std::max(h * 0.5f, v0 * H));
    box.x = (cx - w * 0.5f) / W;
    box.y = (cy - h * 0.5f) / H;
    box.w = w / W;
    box.h = h / H;
    return box;
}

// --- Panneau dans la vidéo --------------------------------------------------------------------------
//
// Un jeu OpenXR soumet l'image de l'œil sans le panneau (le runtime l'ajoute ensuite) : l'enregistreur le
// dessine lui-même, soit à sa place (là où il est vu dans le casque), soit dans un coin de la vidéo.
// Positions données en coordonnées de découpe de Direct3D (x, y, z, w ; x et y de -1 à 1 une fois divisés
// par w, y vers le haut), coins dans l'ordre haut gauche, haut droit, bas gauche, bas droit.

// Coins d'un calque rectangulaire `width` x `height` (mètres) de pose `quadInEye`, dans le repère de l'œil
// (x à droite, y en haut, -z devant) : 4 fois x, y, z.
inline void QuadCorners(const Pose& quadInEye, float width, float height, float out[12]) {
    const float hw = width * 0.5f, hh = height * 0.5f;
    const Vec3 local[4] = {{-hw, hh, 0.f}, {hw, hh, 0.f}, {-hw, -hh, 0.f}, {hw, -hh, 0.f}};
    for (int i = 0; i < 4; ++i) {
        const Vec3 p = quadInEye.position + Rotate(quadInEye.orientation, local[i]);
        out[i * 3] = p.x;
        out[i * 3 + 1] = p.y;
        out[i * 3 + 2] = p.z;
    }
}

// Panneau à sa place : coins (repère de l'œil) vers la vidéo, qui montre `crop` de l'image de l'œil, elle-même
// bordée par les tangentes tanLeft... ; w = profondeur, pour que la carte graphique applique la perspective à
// la texture et coupe ce qui passe à moins de 5 cm devant l'œil (ou derrière). false : rien de visible.
inline bool PanelClipCorners(const float corners[12], float tanLeft, float tanRight, float tanUp, float tanDown, const CropBox& crop, float out[16]) {
    constexpr float kNear = 0.05f;
    const float dx = tanRight - tanLeft, dy = tanUp - tanDown;
    if (!(dx > 1e-4f) || !(dy > 1e-4f) || !(crop.w > 1e-4f) || !(crop.h > 1e-4f)) return false;
    // Écran = a * tangente + b (en x), c * tangente + d (en y) ; multiplié par la profondeur.
    const float a = 2.f / (dx * crop.w), b = 2.f * (-tanLeft / dx - crop.x) / crop.w - 1.f;
    const float c = 2.f / (dy * crop.h), d = 1.f - 2.f * (tanUp / dy - crop.y) / crop.h;
    bool front = false;
    for (int i = 0; i < 4; ++i) {
        const float x = corners[i * 3], y = corners[i * 3 + 1], depth = -corners[i * 3 + 2];
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(depth)) return false;
        out[i * 4] = a * x + b * depth;
        out[i * 4 + 1] = c * y + d * depth;
        out[i * 4 + 2] = depth - kNear;
        out[i * 4 + 3] = depth;
        if (depth > kNear) front = true;
    }
    return front;
}

// Panneau dans un coin (0 haut gauche, 1 haut droit, 2 bas gauche, 3 bas droit) : largeur `size` (part de
// la largeur de la vidéo), hauteur selon `panelRatio` (hauteur / largeur du panneau), marge de 3 % de la
// hauteur de la vidéo. `videoAspect` : largeur / hauteur de la vidéo.
inline void PanelCornerRect(int corner, float size, float panelRatio, float videoAspect, float out[16]) {
    const float my = 0.06f, mx = videoAspect > 0.f ? my / videoAspect : my;
    const float w = 2.f * std::min(0.9f, std::max(0.05f, size));
    const float h = std::min(2.f - 2.f * my, w * std::max(0.f, panelRatio) * videoAspect);
    const float left = (corner & 1) ? 1.f - mx - w : -1.f + mx;
    const float top = (corner & 2) ? -1.f + my + h : 1.f - my;
    const float xs[4] = {left, left + w, left, left + w}, ys[4] = {top, top, top - h, top - h};
    for (int i = 0; i < 4; ++i) {
        out[i * 4] = xs[i];
        out[i * 4 + 1] = ys[i];
        out[i * 4 + 2] = 0.f;
        out[i * 4 + 3] = 1.f;
    }
}

// --- Stabilisation -----------------------------------------------------------------------------------
//
// La vidéo est filmée par une caméra virtuelle qui suit la tête avec un temps de retard : les petites
// secousses disparaissent, les mouvements voulus restent. Seule la rotation compte (une rotation se
// recalcule exactement à partir de l'image de l'œil ; un déplacement non). La caméra garde avec la tête
// lissée la même relation que l'œil avec la tête : sans stabilisation, la vidéo est exactement le cadre
// habituel. Elle ne sort jamais de l'image de l'œil : quand la tête va trop vite, elle est rattrapée.
// Tangentes rangées gauche, droite, haut, bas.

// Rotation qui ramène une direction de la caméra virtuelle dans le repère de l'œil. `smoothed` : orientation
// lissée de la tête (même repère que `eye` et `head`).
inline Quat CameraToEye(const Quat& eye, const Quat& head, const Quat& smoothed) {
    return Mul(Mul(Conjugate(eye), smoothed), Mul(Conjugate(head), eye));
}

// Les quatre coins de la caméra (tangentes `camera`) tombent-ils dans l'image de l'œil (tangentes `eyeTan`) ?
// Une rotation garde les droites : les coins suffisent.
inline bool FrameInside(const Quat& cameraToEye, const float camera[4], const float eyeTan[4]) {
    constexpr float kSlack = 1e-4f;
    for (int i = 0; i < 4; ++i) {
        const Vec3 d = Rotate(cameraToEye, {camera[i & 1], camera[2 + (i >> 1)], -1.f});
        if (!(d.z < -1e-4f)) return false;
        const float x = d.x / -d.z, y = d.y / -d.z;
        if (!(x >= eyeTan[0] - kSlack && x <= eyeTan[1] + kSlack && y <= eyeTan[2] + kSlack && y >= eyeTan[3] - kSlack)) return false;
    }
    return true;
}

// Horizon droit : même direction, sans inclinaison sur le côté (le haut de la scène est +y). Regard
// presque vertical : inchangé (l'horizon n'y a plus de sens).
inline Quat Level(const Quat& q) {
    const Vec3 forward = Rotate(q, {0.f, 0.f, -1.f});
    if (!(std::fabs(forward.y) < 0.97f)) return q;
    Vec3 right = Cross(forward, {0.f, 1.f, 0.f});
    const float n = Length(right);
    if (!(n > 1e-6f)) return q;
    right = right * (1.f / n);
    const Vec3 up = Cross(right, forward);
    return FromBasis(right, up, forward * -1.f);
}

class Stabilizer {
  public:
    void reset() { m_has = false; }

    // Orientation de la caméra virtuelle (repère de la tête) pour cette image vidéo. `dt` : secondes depuis
    // l'image précédente ; `seconds` : temps de retard (0 : aucun lissage) ; `level` : horizon droit.
    // `camera` : cadre de la vidéo dans l'image de l'œil (tangentes), `eyeTan` : bords de l'image de l'œil.
    Quat update(const Quat& eye, const Quat& head, double dt, float seconds, bool level, const float camera[4], const float eyeTan[4]) {
        const auto finite = [](const Quat& q) { return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w); };
        if (!finite(eye) || !finite(head)) {
            m_has = false;
            return head;
        }
        if (!m_has || !(seconds > 0.f)) {
            m_smoothed = head;
            m_has = true;
        } else if (dt > 0.0) {
            const float a = static_cast<float>(1.0 - std::exp(-std::min(dt, 1.0) / static_cast<double>(seconds)));
            m_smoothed = MidOrientation(m_smoothed, head, a);
        }
        Quat target = level ? Level(m_smoothed) : m_smoothed;
        if (FrameInside(CameraToEye(eye, head, target), camera, eyeTan)) return target;
        // Hors de l'image : rapprochée de la tête juste assez (la tête elle-même donne le cadre habituel).
        if (!FrameInside(CameraToEye(eye, head, head), camera, eyeTan)) {
            m_smoothed = head;
            return head;
        }
        float low = 0.f, high = 1.f;
        for (int i = 0; i < 12; ++i) {
            const float mid = (low + high) * 0.5f;
            if (FrameInside(CameraToEye(eye, head, MidOrientation(target, head, mid)), camera, eyeTan))
                high = mid;
            else
                low = mid;
        }
        m_smoothed = MidOrientation(m_smoothed, head, high);
        return MidOrientation(target, head, high);
    }

  private:
    Quat m_smoothed;
    bool m_has = false;
};

// Taille de la vidéo : hauteur choisie (720, 1080, 1440, 2160...), largeur selon le format, nombres
// pairs (exigés par l'encodage 4:2:0) et multiples de 2.
inline void VideoSize(int height, float aspect, int& outWidth, int& outHeight) {
    height = std::max(240, std::min(4320, height));
    outHeight = height & ~1;
    int w = static_cast<int>(std::lround(static_cast<double>(height) * aspect));
    outWidth = std::max(240, std::min(7680, w)) & ~1;
}

// --- Son ---------------------------------------------------------------------------------------------
//
// Une source (son du jeu, micro) livre des paquets datés ; chaque paquet est rangé à sa place sur une
// ligne de temps commune (en images audio de 48 kHz depuis le début de l'enregistrement). Un trou
// (rien ne joue, la capture du son du jeu ne livre alors rien) devient du silence ; un léger décalage
// (gigue de l'horloge) est ignoré ; un paquet qui chevauche ce qui est déjà rangé (horloge de la carte
// son un peu plus rapide) perd la partie en double. Échantillons flottants stéréo entrelacés.
class AudioTimeline {
  public:
    static constexpr int kChannels = 2;

    explicit AudioTimeline(int64_t tolerance = 480) : m_tolerance(tolerance) {}

    void reset(int64_t start = 0) {
        m_data.clear();
        m_base = start;
    }

    // Prochaine position attendue (fin de ce qui est rangé).
    int64_t end() const { return m_base + static_cast<int64_t>(m_data.size() / kChannels); }

    // `frames` images (paires gauche/droite) commençant à la position `pos`.
    void write(int64_t pos, const float* samples, int64_t frames) {
        if (frames <= 0) return;
        int64_t skip = 0;
        if (pos < m_base) {  // déjà lu : trop tard
            skip = m_base - pos;
            pos = m_base;
        }
        const int64_t tail = end();
        const int64_t limitFrames = 48000 * 10;
        if (pos > tail + m_tolerance) {
            // Trou : du silence, mais jamais plus que ce qui est gardé en mémoire (position aberrante).
            if (pos - tail >= limitFrames) {
                m_data.clear();
                m_base = pos;
            } else {
                m_data.insert(m_data.end(), static_cast<size_t>((pos - tail) * kChannels), 0.f);
            }
        } else if (pos < tail - m_tolerance) {
            skip += tail - pos;
        }
        if (skip >= frames) return;
        m_data.insert(m_data.end(), samples + skip * kChannels, samples + frames * kChannels);
        // Pas plus de 10 s d'avance en mémoire (lecture arrêtée) : on oublie le plus ancien.
        const size_t limit = static_cast<size_t>(limitFrames * kChannels);
        if (m_data.size() > limit) {
            const size_t drop = (m_data.size() - limit) / kChannels * kChannels;
            m_data.erase(m_data.begin(), m_data.begin() + static_cast<std::ptrdiff_t>(drop));
            m_base += static_cast<int64_t>(drop / kChannels);
        }
    }

    // Ajoute (mélange) à `out` les images [from, from + frames) et les retire ; silence là où rien n'est rangé.
    void mixInto(int64_t from, float* out, int64_t frames, float gain = 1.f) {
        if (frames <= 0) return;
        if (from > m_base) {
            const int64_t drop = std::min<int64_t>(from - m_base, static_cast<int64_t>(m_data.size() / kChannels));
            m_data.erase(m_data.begin(), m_data.begin() + static_cast<std::ptrdiff_t>(drop * kChannels));
            m_base = from;
        }
        const int64_t offset = m_base - from;  // > 0 : début non disponible (silence)
        const int64_t available = static_cast<int64_t>(m_data.size() / kChannels);
        const int64_t count = std::max<int64_t>(0, std::min(frames - offset, available));
        for (int64_t i = 0; i < count; ++i)
            for (int c = 0; c < kChannels; ++c)
                out[(offset + i) * kChannels + c] += m_data[static_cast<size_t>(i * kChannels + c)] * gain;
        m_data.erase(m_data.begin(), m_data.begin() + static_cast<std::ptrdiff_t>(count * kChannels));
        // Ce qui reste (venant après la lecture) garde sa place ; plus rien : on attend la suite de la lecture.
        if (m_data.empty())
            m_base = std::max(m_base + count, from + frames);
        else
            m_base += count;
    }

  private:
    std::deque<float> m_data;
    int64_t m_base = 0;  // position de la première image rangée
    int64_t m_tolerance;
};

// Conversion en 16 bits avec écrêtage (l'encodeur AAC de Windows ne prend que du 16 bits).
inline int16_t ToPcm16(float v) {
    if (!(v == v)) return 0;
    v = std::max(-1.f, std::min(1.f, v));
    return static_cast<int16_t>(std::lround(v * 32767.f));
}

// Ramène un paquet de `channels` canaux à du stéréo (mono dupliqué ; au-delà de deux canaux, avant gauche
// et avant droit, plus le centre à moitié s'il existe : 3 canaux, ou 5.1 et 7.1).
inline void ToStereo(const float* in, int channels, int64_t frames, float* out) {
    for (int64_t i = 0; i < frames; ++i) {
        const float* f = in + i * channels;
        float l, r;
        if (channels <= 1) {
            l = r = f[0];
        } else {
            l = f[0];
            r = f[1];
            if (channels == 3 || channels >= 6) {
                l += f[2] * 0.5f;
                r += f[2] * 0.5f;
            }
        }
        out[i * 2] = l;
        out[i * 2 + 1] = r;
    }
}

}  // namespace xrpo
