// Réglages des filtres d'image (layer, enregistreur vidéo) et leur conversion pour le shader
// (image_filter_entry.hlsli). Code portable, testé dans tests/preview.cpp.
#pragma once

#include <cstdint>

namespace xrpo {

// Réglages tels qu'on les choisit (page Image de XRPerfSensors, section [image] du .ini).
struct ImageSettings {
    int mode = 0;         // 0 aucun filtre, 1 dans le casque (et la vidéo), 2 dans la vidéo seulement
    int sharpen = 0;      // 0 aucune netteté, 1 CAS, 2 RCAS, 3 LumaSharpen
    int sharpness = 50;   // force de la netteté, 0 à 200 (LumaSharpen 100 = force 2 de ReShade)
    int sharpenLimit = 4; // LumaSharpen : écart maximal ajouté, en % de luminosité, 1 à 50 (« clamp » x 100)
    int sharpenRadius = 1;// portée de la netteté, 1 à 3 pixels
    int brightness = 0;   // -50 à 50
    int contrast = 0;     // -50 à 50
    int saturation = 0;   // -100 à 100
    int vibrance = 0;     // 0 à 100
    int gamma = 100;      // 50 à 200 (1 = 100)
    int hdr = 0;          // faux HDR (contraste local), 0 à 100
    int shadows = 0;      // 0 à 100
    int temperature = 0;  // -50 (froid) à 50 (chaud)
    int deband = 0;       // 0 à 100
    int colorblind = 0;   // 0 aucun, 1 protanopie, 2 deutéranopie, 3 tritanopie

    // Valeurs ramenées dans leurs bornes.
    void clamp() {
        const auto c = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
        // Choix inconnu (version plus récente de XRPerfSensors...) : désactivé, jamais un autre choix.
        const auto pick = [](int v, int count) { return v >= 0 && v < count ? v : 0; };
        mode = pick(mode, 3);
        sharpen = pick(sharpen, 4);
        sharpness = c(sharpness, 0, 200);
        sharpenLimit = c(sharpenLimit, 1, 50);
        sharpenRadius = c(sharpenRadius, 1, 3);
        brightness = c(brightness, -50, 50);
        contrast = c(contrast, -50, 50);
        saturation = c(saturation, -100, 100);
        vibrance = c(vibrance, 0, 100);
        gamma = c(gamma, 50, 200);
        shadows = c(shadows, 0, 100);
        temperature = c(temperature, -50, 50);
        deband = c(deband, 0, 100);
        hdr = c(hdr, 0, 100);
        colorblind = pick(colorblind, 4);
    }

    // Un réglage change l'image (sinon, rien à faire).
    bool changesImage() const {
        return sharpen != 0 || brightness != 0 || contrast != 0 || saturation != 0 || vibrance != 0 || gamma != 100 || shadows != 0 ||
               temperature != 0 || deband != 0 || colorblind != 0 || hdr != 0;
    }

    bool operator==(const ImageSettings& o) const {
        return mode == o.mode && sharpen == o.sharpen && sharpness == o.sharpness && sharpenLimit == o.sharpenLimit &&
               sharpenRadius == o.sharpenRadius && brightness == o.brightness && contrast == o.contrast &&
               saturation == o.saturation && vibrance == o.vibrance && gamma == o.gamma && shadows == o.shadows && temperature == o.temperature &&
               deband == o.deband && colorblind == o.colorblind && hdr == o.hdr;
    }
    bool operator!=(const ImageSettings& o) const { return !(*this == o); }
};

// Constantes du shader (20 valeurs, dans l'ordre de image_filter_entry.hlsli).
struct ImageFilterParams {
    float sharpenMode = 0.f, sharpenAmount = 0.f, deband = 0.f, brightness = 0.f;
    float contrast = 0.f, saturation = 0.f, vibrance = 0.f, gammaInverse = 1.f;
    float shadows = 0.f, temperature = 0.f, colorblind = 0.f, seed = 0.f;
    float originX = 0.f, originY = 0.f, inLinear = 1.f, outLinear = 1.f;
    float width = 0.f, height = 0.f, hdr = 0.f, reserved = 0.f;
    float sharpenLimit = 0.04f, sharpenRadius = 1.f, reserved1 = 0.f, reserved2 = 0.f;
};
static_assert(sizeof(ImageFilterParams) == 96, "24 valeurs (6 x float4)");

inline ImageFilterParams ImageParamsFrom(ImageSettings s) {
    s.clamp();
    ImageFilterParams p;
    p.sharpenMode = static_cast<float>(s.sharpen);
    p.sharpenAmount = s.sharpness / 100.f;
    p.deband = s.deband / 100.f;
    p.brightness = s.brightness / 200.f;  // -0,25 à 0,25
    p.contrast = s.contrast / 100.f;      // -0,5 à 0,5
    p.saturation = s.saturation / 100.f;
    p.vibrance = s.vibrance / 100.f;
    p.gammaInverse = 100.f / static_cast<float>(s.gamma);
    p.shadows = s.shadows / 100.f;
    p.temperature = s.temperature / 50.f;
    p.colorblind = static_cast<float>(s.colorblind);
    p.hdr = s.hdr / 100.f;
    p.sharpenLimit = s.sharpenLimit / 100.f;
    p.sharpenRadius = static_cast<float>(s.sharpenRadius);
    return p;
}

}  // namespace xrpo
