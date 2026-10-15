// Mise en forme de l'image d'une fenêtre capturée (XRPerfCapture.dll) : réduction et opacité.
// Code portable, testé hors de Windows.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace xrpo {

// Zone gardée d'une image de `width` x `height` après recadrage : parts retirées de chaque côté,
// bornées à 45 %, et au moins 16 pixels gardés dans chaque sens (sinon pas de recadrage dans ce sens).
struct CropRect {
    int left = 0, top = 0, right = 0, bottom = 0;
    int width() const { return right - left; }
    int height() const { return bottom - top; }
};

inline CropRect WindowCrop(int width, int height, float left, float top, float right, float bottom) {
    const auto part = [](float value) { return value >= 0.f && value <= 0.45f ? value : (value > 0.45f ? 0.45f : 0.f); };  // NaN : 0
    const auto round = [](float value) { return static_cast<int>(value + 0.5f); };
    CropRect r;
    r.left = round(width * part(left));
    r.right = width - round(width * part(right));
    r.top = round(height * part(top));
    r.bottom = height - round(height * part(bottom));
    if (r.right - r.left < 16) r.left = 0, r.right = width;
    if (r.bottom - r.top < 16) r.top = 0, r.bottom = height;
    return r;
}

// Taille écrite dans la mémoire partagée : l'image réduite (proportions gardées) si elle dépasse
// `maxWidth` x `maxHeight`.
inline void WindowOutputSize(int width, int height, int maxWidth, int maxHeight, int& outWidth, int& outHeight) {
    const double scale = std::min(1.0, std::min(static_cast<double>(maxWidth) / width, static_cast<double>(maxHeight) / height));
    outWidth = std::max(1, std::min(maxWidth, static_cast<int>(width * scale)));
    outHeight = std::max(1, std::min(maxHeight, static_cast<int>(height * scale)));
}

// Réduction par moyenne des pixels couverts (ou simple copie à l'échelle 1), image rendue opaque
// puis multipliée par l'opacité : couleurs et alpha prémultipliés.
inline void DownscaleWindowImage(const uint8_t* src, int srcPitch, int srcWidth, int srcHeight, uint8_t* dst, int dstWidth, int dstHeight, uint32_t alpha) {
    if (dstWidth == srcWidth && dstHeight == srcHeight) {
        for (int y = 0; y < dstHeight; ++y) {
            const uint8_t* s = src + static_cast<size_t>(y) * srcPitch;
            uint8_t* d = dst + static_cast<size_t>(y) * dstWidth * 4;
            if (alpha == 255) {
                std::memcpy(d, s, static_cast<size_t>(dstWidth) * 4);
                for (int x = 0; x < dstWidth; ++x) d[x * 4 + 3] = 255;
            } else {
                for (int x = 0; x < dstWidth; ++x) {
                    d[x * 4] = static_cast<uint8_t>((s[x * 4] * alpha + 127) / 255);
                    d[x * 4 + 1] = static_cast<uint8_t>((s[x * 4 + 1] * alpha + 127) / 255);
                    d[x * 4 + 2] = static_cast<uint8_t>((s[x * 4 + 2] * alpha + 127) / 255);
                    d[x * 4 + 3] = static_cast<uint8_t>(alpha);
                }
            }
        }
        return;
    }
    std::vector<int> x0(static_cast<size_t>(dstWidth) + 1);
    for (int x = 0; x <= dstWidth; ++x) x0[static_cast<size_t>(x)] = static_cast<int>(static_cast<int64_t>(x) * srcWidth / dstWidth);
    std::vector<uint32_t> sums(static_cast<size_t>(dstWidth) * 3);
    for (int y = 0; y < dstHeight; ++y) {
        const int ya = static_cast<int>(static_cast<int64_t>(y) * srcHeight / dstHeight);
        int yb = static_cast<int>(static_cast<int64_t>(y + 1) * srcHeight / dstHeight);
        if (yb <= ya) yb = ya + 1;
        std::fill(sums.begin(), sums.end(), 0u);
        for (int sy = ya; sy < yb; ++sy) {
            const uint8_t* s = src + static_cast<size_t>(sy) * srcPitch;
            for (int x = 0; x < dstWidth; ++x) {
                int xa = x0[static_cast<size_t>(x)], xb = x0[static_cast<size_t>(x) + 1];
                if (xb <= xa) xb = xa + 1;
                uint32_t b = 0, g = 0, r = 0;
                for (int sx = xa; sx < xb; ++sx) {
                    b += s[sx * 4];
                    g += s[sx * 4 + 1];
                    r += s[sx * 4 + 2];
                }
                sums[static_cast<size_t>(x) * 3] += b;
                sums[static_cast<size_t>(x) * 3 + 1] += g;
                sums[static_cast<size_t>(x) * 3 + 2] += r;
            }
        }
        uint8_t* d = dst + static_cast<size_t>(y) * dstWidth * 4;
        for (int x = 0; x < dstWidth; ++x) {
            int xa = x0[static_cast<size_t>(x)], xb = x0[static_cast<size_t>(x) + 1];
            if (xb <= xa) xb = xa + 1;
            const uint64_t count = static_cast<uint64_t>(xb - xa) * static_cast<uint64_t>(yb - ya);
            for (int c = 0; c < 3; ++c) {
                const uint64_t value = (static_cast<uint64_t>(sums[static_cast<size_t>(x) * 3 + c]) * alpha + count * 255 / 2) / (count * 255);
                d[x * 4 + c] = static_cast<uint8_t>(value > 255 ? 255 : value);
            }
            d[x * 4 + 3] = static_cast<uint8_t>(alpha);
        }
    }
}

}  // namespace xrpo
