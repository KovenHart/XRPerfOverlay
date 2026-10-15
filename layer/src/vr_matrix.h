// Conversions entre une pose (position + quaternion) et une matrice 3 x 4 d'OpenVR (HmdMatrix34_t :
// rotation dans les trois premières colonnes, position dans la quatrième). Code portable, testé dans
// tests/preview.cpp.
#pragma once

#include "pose_math.h"

namespace xrpo {

inline void PoseToMatrix34(const Pose& p, float (&m)[3][4]) {
    const Quat& q = p.orientation;
    m[0][0] = 1.f - 2.f * (q.y * q.y + q.z * q.z);
    m[0][1] = 2.f * (q.x * q.y - q.z * q.w);
    m[0][2] = 2.f * (q.x * q.z + q.y * q.w);
    m[1][0] = 2.f * (q.x * q.y + q.z * q.w);
    m[1][1] = 1.f - 2.f * (q.x * q.x + q.z * q.z);
    m[1][2] = 2.f * (q.y * q.z - q.x * q.w);
    m[2][0] = 2.f * (q.x * q.z - q.y * q.w);
    m[2][1] = 2.f * (q.y * q.z + q.x * q.w);
    m[2][2] = 1.f - 2.f * (q.x * q.x + q.y * q.y);
    m[0][3] = p.position.x;
    m[1][3] = p.position.y;
    m[2][3] = p.position.z;
}

inline Pose Matrix34ToPose(const float (&m)[3][4]) {
    const auto normalized = [](Vec3 v) {
        const float length = Length(v);
        return length > 1e-6f ? v * (1.f / length) : v;
    };
    Pose p;
    p.position = {m[0][3], m[1][3], m[2][3]};
    // Colonnes = axes du repère ; normalisées par précaution (léger facteur d'échelle possible).
    p.orientation = FromBasis(normalized({m[0][0], m[1][0], m[2][0]}), normalized({m[0][1], m[1][1], m[2][1]}),
                              normalized({m[0][2], m[1][2], m[2][2]}));
    return p;
}

}  // namespace xrpo
