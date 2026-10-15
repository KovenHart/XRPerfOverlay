// Petits calculs de positions et d'orientations pour accrocher un calque à une main : emplacements
// sur la manette (dos ou intérieur du poignet, au-dessus de la manette) et test « l'utilisateur
// regarde-t-il son poignet ? ». Code portable, testé dans tests/preview.cpp.
//
// Repère de la poignée OpenXR (« grip pose ») : -Z dans l'axe de la manette, vers l'avant ; +Y vers
// le haut, à peu près dans la direction du pouce ; +X vers la droite pour les deux mains, c'est-à-dire
// vers l'extérieur de la paume pour la main gauche et vers le dos de la main pour la main droite.
// Un calque OpenXR (quad) est vu de face le long de son +Z, avec son haut le long de son +Y.
#pragma once

#include <cmath>

namespace xrpo {

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
};
struct Quat {
    float x = 0.f, y = 0.f, z = 0.f, w = 1.f;
};
struct Pose {
    Vec3 position;
    Quat orientation;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }

inline Quat Mul(Quat a, Quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

inline Vec3 Rotate(Quat q, Vec3 v) {
    const Vec3 u = {q.x, q.y, q.z};
    const Vec3 t = Cross(u, v) * 2.f;
    return v + t * q.w + Cross(u, t);
}

// Pose `local` exprimée dans le repère `parent`.
inline Pose Compose(const Pose& parent, const Pose& local) {
    return {parent.position + Rotate(parent.orientation, local.position), Mul(parent.orientation, local.orientation)};
}

// Pose inverse : passe du repère de `pose` au repère parent.
inline Pose Inverse(const Pose& pose) {
    const Quat q = {-pose.orientation.x, -pose.orientation.y, -pose.orientation.z, pose.orientation.w};
    return {Rotate(q, pose.position * -1.f), q};
}

// Rotation dont les axes X, Y, Z (orthonormés, repère direct) sont donnés.
inline Quat FromBasis(Vec3 X, Vec3 Y, Vec3 Z) {
    Quat q;
    const float trace = X.x + Y.y + Z.z;
    if (trace > 0.f) {
        const float s = std::sqrt(trace + 1.f) * 2.f;
        q = {(Y.z - Z.y) / s, (Z.x - X.z) / s, (X.y - Y.x) / s, 0.25f * s};
    } else if (X.x > Y.y && X.x > Z.z) {
        const float s = std::sqrt(1.f + X.x - Y.y - Z.z) * 2.f;
        q = {0.25f * s, (Y.x + X.y) / s, (Z.x + X.z) / s, (Y.z - Z.y) / s};
    } else if (Y.y > Z.z) {
        const float s = std::sqrt(1.f + Y.y - X.x - Z.z) * 2.f;
        q = {(Y.x + X.y) / s, 0.25f * s, (Z.y + Y.z) / s, (Z.x - X.z) / s};
    } else {
        const float s = std::sqrt(1.f + Z.z - X.x - Y.y) * 2.f;
        q = {(Z.x + X.z) / s, (Z.y + Y.z) / s, 0.25f * s, (X.y - Y.x) / s};
    }
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}

enum class Hand { Left = 0, Right = 1 };

// Emplacement sur la main. L'ordre est celui de la liste de XRPerfSensors et des noms du .ini.
enum class HandPlacement {
    WristBack = 0,  // dos du poignet, comme une montre
    WristInside,    // intérieur du poignet
    Above,          // au-dessus de la manette, tourné vers l'utilisateur
    Count
};

// Pose du calque dans le repère de la poignée. `along` : déplacement le long de l'avant-bras (m,
// positif vers le coude) ; `lift` : écart à la surface (m, positif vers l'extérieur). Tous deux
// s'ajoutent aux valeurs par défaut de l'emplacement.
inline Pose HandPlacementPose(Hand hand, HandPlacement placement, float along = 0.f, float lift = 0.f) {
    const float side = hand == Hand::Left ? -1.f : 1.f;  // -1 : la main gauche est le miroir de la droite
    const Vec3 gx = {1.f, 0.f, 0.f}, gy = {0.f, 1.f, 0.f}, gz = {0.f, 0.f, 1.f};
    Vec3 X, Y, Z, origin;
    switch (placement) {
        case HandPlacement::WristInside:
            // Paume tournée vers le haut : le haut du cadran est côté pouce, sa face côté paume.
            Z = gx * -side;
            Y = gy;
            X = Cross(Y, Z);
            origin = gz * (0.095f + along) + Z * (0.035f + lift);
            break;
        case HandPlacement::Above: {
            // Au-dessus de la manette, incliné de 30° vers l'arrière pour faire face aux yeux.
            const float c = std::cos(0.5236f), s = std::sin(0.5236f);
            Z = gz * c + gy * s;
            Y = gy * c - gz * s;
            X = Cross(Y, Z);
            origin = gy * (0.07f + lift) + gz * (0.02f + along);
            break;
        }
        case HandPlacement::WristBack:
        default:
            // Dos de la main tourné vers les yeux : le haut du cadran est à l'opposé du pouce.
            Z = gx * side;
            Y = gy * -1.f;
            X = Cross(Y, Z);
            origin = gz * (0.095f + along) + Z * (0.04f + lift);
            break;
    }
    return {origin, FromBasis(X, Y, Z)};
}

// Le calque est-il regardé ? `inView` : sa pose dans le repère de la tête (yeux à l'origine, regard
// vers -Z). Il faut qu'il soit à peu près face aux yeux et pas trop loin de l'axe du regard. Les
// seuils diffèrent selon qu'il est déjà affiché ou non, pour éviter un clignotement à la limite.
inline bool IsLookedAt(const Pose& inView, bool shown) {
    const Vec3 c = inView.position;
    const float distance = Length(c);
    if (distance < 0.05f || distance > 1.5f) return false;
    const Vec3 toEyes = c * (-1.f / distance);
    const float facing = Dot(Rotate(inView.orientation, {0.f, 0.f, 1.f}), toEyes);  // 1 = pile en face
    const float centered = -c.z / distance;                                         // 1 = dans l'axe du regard
    return shown ? (facing > 0.35f && centered > 0.60f) : (facing > 0.55f && centered > 0.75f);
}

}  // namespace xrpo
