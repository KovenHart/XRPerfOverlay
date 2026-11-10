// Ce qu'il faut de HLSL en C++ pour compiler layer/src/image_filter_core.hlsli dans les tests : le type
// float3 et les fonctions intrinsèques qu'il utilise, composante par composante.
#pragma once

#include <algorithm>
#include <cmath>

namespace shader {

struct float3 {
    float x = 0.f, y = 0.f, z = 0.f;
    float3() = default;
    float3(float a, float b, float c) : x(a), y(b), z(c) {}
};

inline float3 operator+(float3 a, float3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline float3 operator-(float3 a, float3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline float3 operator*(float3 a, float3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline float3 operator/(float3 a, float3 b) { return {a.x / b.x, a.y / b.y, a.z / b.z}; }
inline float3 operator*(float3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float3 operator*(float s, float3 a) { return a * s; }
inline float3 operator/(float3 a, float s) { return {a.x / s, a.y / s, a.z / s}; }
inline float3 operator-(float3 a) { return {-a.x, -a.y, -a.z}; }

inline float min(float a, float b) { return std::min(a, b); }
inline float max(float a, float b) { return std::max(a, b); }
inline float3 min(float3 a, float3 b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline float3 max(float3 a, float3 b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
inline float abs(float a) { return std::fabs(a); }
inline float3 abs(float3 a) { return {std::fabs(a.x), std::fabs(a.y), std::fabs(a.z)}; }
inline float saturate(float a) { return std::min(1.f, std::max(0.f, a)); }
inline float3 saturate(float3 a) { return {saturate(a.x), saturate(a.y), saturate(a.z)}; }
inline float clamp(float a, float lo, float hi) { return std::min(hi, std::max(lo, a)); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float3 lerp(float3 a, float3 b, float t) { return a + (b - a) * t; }
inline float dot(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float3 sqrt(float3 a) { return {std::sqrt(a.x), std::sqrt(a.y), std::sqrt(a.z)}; }
inline float pow(float a, float b) { return std::pow(a, b); }
inline float3 pow(float3 a, float3 b) { return {std::pow(a.x, b.x), std::pow(a.y, b.y), std::pow(a.z, b.z)}; }

}  // namespace shader
