// Filtres d'image : netteté (CAS, RCAS, LumaSharpen), débandage, couleurs, mode daltonien.
//
// Ce fichier est à la fois du HLSL (compilé à l'exécution par la layer et l'enregistreur vidéo, inclus
// dans leur source par CMake : image_filter_source.h) et du C++ (tests/preview.cpp, avec les types et
// fonctions de tests/shader_shim.h) : il s'en tient au sous-ensemble commun (float, float3, fonctions
// usuelles, littéraux en f, pas de mélange de swizzles). Les valeurs sont encodées en sRGB (0 à 1) :
// les réglages agissent comme on les perçoit.
//
// Réglages (ImageSettings, voir FilterPixel) : nuls = image inchangée.

static const float3 kLumaWeights = float3(0.2126f, 0.7152f, 0.0722f);

float Luma(float3 c) { return dot(c, kLumaWeights); }

float Max3(float3 c) { return max(c.x, max(c.y, c.z)); }

float Min3(float3 c) { return min(c.x, min(c.y, c.z)); }

// Conversion sRGB : valeurs linéaires (formats non sRGB d'OpenXR, vue sRGB) <-> encodées.
float3 ToGamma(float3 c) {
    c = saturate(c);
    float3 low = c * 12.92f;
    float3 high = 1.055f * pow(c, float3(1.0f / 2.4f, 1.0f / 2.4f, 1.0f / 2.4f)) - float3(0.055f, 0.055f, 0.055f);
    return float3(c.x <= 0.0031308f ? low.x : high.x, c.y <= 0.0031308f ? low.y : high.y, c.z <= 0.0031308f ? low.z : high.z);
}

float3 ToLinear(float3 c) {
    c = saturate(c);
    float3 low = c / 12.92f;
    float3 high = pow((c + float3(0.055f, 0.055f, 0.055f)) / 1.055f, float3(2.4f, 2.4f, 2.4f));
    return float3(c.x <= 0.04045f ? low.x : high.x, c.y <= 0.04045f ? low.y : high.y, c.z <= 0.04045f ? low.z : high.z);
}

// --- Netteté --------------------------------------------------------------------------------------
// Voisinage 3 x 3 :   a b c
//                     d e f
//                     g h i
// `amount` : 0 à 1.

// CAS (AMD FidelityFX Contrast Adaptive Sharpening, licence MIT) : accentue moins là où le contraste
// est déjà fort, sans halo.
float3 SharpenCas(float3 a, float3 b, float3 c, float3 d, float3 e, float3 f, float3 g, float3 h, float3 i, float amount) {
    float3 mnCross = min(min(min(b, d), min(e, f)), h);
    float3 mnAll = min(mnCross, min(min(a, c), min(g, i)));
    float3 mn = mnCross + mnAll;
    float3 mxCross = max(max(max(b, d), max(e, f)), h);
    float3 mxAll = max(mxCross, max(max(a, c), max(g, i)));
    float3 mx = mxCross + mxAll;
    float3 amp = saturate(min(mn, float3(2.0f, 2.0f, 2.0f) - mx) / max(mx, float3(1e-5f, 1e-5f, 1e-5f)));
    amp = sqrt(amp);
    float peak = -1.0f / lerp(8.0f, 5.0f, saturate(amount));
    float3 w = amp * peak;
    float3 weight = float3(1.0f, 1.0f, 1.0f) + 4.0f * w;
    return saturate((b * w + d * w + f * w + h * w + e) / weight);
}

// RCAS (« Robust CAS » de FSR 1, licence MIT) : plus fort, limité pour ne jamais sortir des valeurs
// voisines (pas de halo, pas d'écrêtage).
float3 SharpenRcas(float3 b, float3 d, float3 e, float3 f, float3 h, float amount) {
    float3 mn4 = min(min(b, d), min(f, h));
    float3 mx4 = max(max(b, d), max(f, h));
    float3 hitMin = min(mn4, e) / max(4.0f * mx4, float3(1e-5f, 1e-5f, 1e-5f));
    float3 hitMax = (float3(1.0f, 1.0f, 1.0f) - max(mx4, e)) / min(4.0f * mn4 - float3(4.0f, 4.0f, 4.0f), float3(-1e-5f, -1e-5f, -1e-5f));
    float3 lobeRgb = max(-hitMin, hitMax);
    float lobe = max(-0.1875f, min(Max3(lobeRgb), 0.0f)) * saturate(amount);
    return saturate((lobe * (b + d + f + h) + e) / (4.0f * lobe + 1.0f));
}

// LumaSharpen : accentue la luminosité seule (les couleurs ne bougent pas), écart borné.
float3 SharpenLuma(float3 a, float3 b, float3 c, float3 d, float3 e, float3 f, float3 g, float3 h, float3 i, float amount) {
    float3 blur = (a + c + g + i) * 0.0625f + (b + d + f + h) * 0.125f + e * 0.25f;
    float detail = Luma(e - blur) * (1.5f * saturate(amount));
    detail = clamp(detail, -0.035f, 0.035f);
    return saturate(e + float3(detail, detail, detail));
}

// --- Débandage ------------------------------------------------------------------------------------
// Dans une zone presque unie (les quatre échantillons lointains proches du centre), le pixel prend leur
// moyenne : les marches d'un dégradé disparaissent. `strength` : 0 à 1.
float3 Deband(float3 original, float3 current, float3 s1, float3 s2, float3 s3, float3 s4, float strength) {
    float threshold = 0.004f + 0.012f * saturate(strength);
    float3 average = (s1 + s2 + s3 + s4) * 0.25f;
    float3 diff = max(max(abs(s1 - original), abs(s2 - original)), max(abs(s3 - original), abs(s4 - original)));
    return Max3(diff) < threshold ? average : current;
}

// --- Couleurs -------------------------------------------------------------------------------------
// brightness, contrast : -0,5 à 0,5 ; saturation : -1 à 1 ; vibrance, shadows : 0 à 1 ; gammaInverse :
// 1 / gamma ; temperature : -1 (froid) à 1 (chaud).
float3 AdjustColor(float3 c, float brightness, float contrast, float saturation, float vibrance, float gammaInverse, float shadows,
                   float temperature) {
    // Ombres : courbe nulle au noir et au blanc, qui remonte surtout les tons sombres (le noir reste noir).
    float3 one = float3(1.0f, 1.0f, 1.0f);
    float3 dark = (one - c) * (one - c) * (one - c);
    c = c + (1.5f * shadows) * c * dark;
    c = pow(saturate(c), float3(gammaInverse, gammaInverse, gammaInverse));
    c = c + float3(brightness, brightness, brightness);
    c = (c - float3(0.5f, 0.5f, 0.5f)) * (1.0f + contrast) + float3(0.5f, 0.5f, 0.5f);
    c = saturate(c);
    float luma = Luma(c);
    float3 gray = float3(luma, luma, luma);
    // Vibrance : sature davantage ce qui l'est peu.
    float spread = Max3(c) - Min3(c);
    c = lerp(gray, c, 1.0f + saturation + vibrance * (1.0f - spread));
    c = c * float3(1.0f + 0.1f * temperature, 1.0f, 1.0f - 0.1f * temperature);
    return saturate(c);
}

// --- Mode daltonien -------------------------------------------------------------------------------
// Daltonisation (simulation dans l'espace LMS, puis report de l'information perdue sur les couleurs
// encore distinguées). mode : 1 protanopie, 2 deutéranopie, 3 tritanopie.
float3 Daltonize(float3 c, float mode) {
    float l = 17.8824f * c.x + 43.5161f * c.y + 4.11935f * c.z;
    float m = 3.45565f * c.x + 27.1554f * c.y + 3.86714f * c.z;
    float s = 0.0299566f * c.x + 0.184309f * c.y + 1.46709f * c.z;
    if (mode < 1.5f) {
        l = 2.02344f * m - 2.52581f * s;
    } else if (mode < 2.5f) {
        m = 0.494207f * l + 1.24827f * s;
    } else {
        s = -0.395913f * l + 0.801109f * m;
    }
    float3 simulated = float3(0.0809444479f * l - 0.130504409f * m + 0.116721066f * s,
                              -0.0102485335f * l + 0.0540193266f * m - 0.113614708f * s,
                              -0.000365296938f * l - 0.00412161469f * m + 0.693511405f * s);
    float3 error = c - simulated;
    float3 shift = float3(0.0f, 0.7f * error.x + error.y, 0.7f * error.x + error.z);
    return saturate(c + shift);
}

// --- Tout, pour un pixel --------------------------------------------------------------------------
// sharpenMode : 0 aucune, 1 CAS, 2 RCAS, 3 LumaSharpen. far1 à far4 : échantillons lointains (débandage),
// noise : -0,5 à 0,5 (tramage, avec le débandage). colorblind : 0 à 3.
float3 FilterPixel(float3 a, float3 b, float3 c, float3 d, float3 e, float3 f, float3 g, float3 h, float3 i, float3 far1, float3 far2, float3 far3,
                   float3 far4, float noise, float sharpenMode, float sharpenAmount, float deband, float brightness, float contrast, float saturation,
                   float vibrance, float gammaInverse, float shadows, float temperature, float colorblind) {
    float3 color = e;
    if (sharpenMode > 2.5f) {
        color = SharpenLuma(a, b, c, d, e, f, g, h, i, sharpenAmount);
    } else if (sharpenMode > 1.5f) {
        color = SharpenRcas(b, d, e, f, h, sharpenAmount);
    } else if (sharpenMode > 0.5f) {
        color = SharpenCas(a, b, c, d, e, f, g, h, i, sharpenAmount);
    }
    if (deband > 0.0f) color = Deband(e, color, far1, far2, far3, far4, deband);
    color = AdjustColor(color, brightness, contrast, saturation, vibrance, gammaInverse, shadows, temperature);
    if (colorblind > 0.5f) color = Daltonize(color, colorblind);
    if (deband > 0.0f) color = saturate(color + float3(noise, noise, noise) / 255.0f);
    return color;
}
