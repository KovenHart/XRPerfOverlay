// Passe de filtres d'image (HLSL seulement), après image_filter_core.hlsli : un triangle couvre la zone
// filtrée (fenêtre d'affichage = rectangle de l'œil), chaque pixel lit son voisinage dans une copie de
// cette zone. Constantes en mémoire tampon b0 (Direct3D 11) ou constantes racine b0 (Direct3D 12) :
// 20 valeurs, voir ImageFilterParams (image_filter.h).

cbuffer ImageParams : register(b0) {
    float4 p0;  // netteté (0 à 3), force de la netteté, débandage, luminosité
    float4 p1;  // contraste, saturation, vibrance, 1 / gamma
    float4 p2;  // ombres, température, daltonisme (0 à 3), graine du bruit
    float4 p3;  // origine de la zone dans la cible (pixels), entrée linéaire, sortie linéaire
    float4 p4;  // taille de la zone (pixels), faux HDR, libre
    float4 p5;  // limite de LumaSharpen, portée de la netteté (pixels), libres
};
Texture2D<float4> source : register(t0);

float4 VsFilter(uint id : SV_VertexID) : SV_Position {
    float2 t = float2((id << 1) & 2, id & 2);
    return float4(t * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}

float3 Fetch(int2 at) {
    int2 size = int2(p4.xy);
    int2 q = clamp(at, int2(0, 0), size - int2(1, 1));
    float3 c = source.Load(int3(q, 0)).rgb;
    return p3.z > 0.5f ? ToGamma(c) : saturate(c);
}

float Hash(float2 p) { return frac(sin(dot(p, float2(12.9898f, 78.233f))) * 43758.5453f); }

float4 PsFilter(float4 position : SV_Position) : SV_Target {
    int2 at = int2(position.xy - p3.xy);
    // Voisinage de la netteté à `r` pixels : 2 ou 3 quand l'image est suréchantillonnée, pour que l'effet
    // survive à sa réduction vers l'écran du casque.
    int r = clamp(int(p5.y), 1, 3);
    float3 a = Fetch(at + int2(-r, -r));
    float3 b = Fetch(at + int2(0, -r));
    float3 c = Fetch(at + int2(r, -r));
    float3 d = Fetch(at + int2(-r, 0));
    float3 e = Fetch(at);
    float3 f = Fetch(at + int2(r, 0));
    float3 g = Fetch(at + int2(-r, r));
    float3 h = Fetch(at + int2(0, r));
    float3 i = Fetch(at + int2(r, r));
    // Débandage : quatre échantillons à 8-24 pixels, dans une direction tirée au hasard pour chaque pixel.
    float3 far1 = e, far2 = e, far3 = e, far4 = e;
    float noise = 0.0f;
    if (p0.z > 0.0f) {
        float2 seed = float2(at) + float2(p2.w, p2.w * 0.37f);
        float angle = Hash(seed) * 6.2831853f;
        float radius = 8.0f + 16.0f * Hash(seed + float2(17.0f, 59.0f));
        int2 o1 = int2(round(float2(cos(angle), sin(angle)) * radius));
        int2 o2 = int2(-o1.y, o1.x);
        far1 = Fetch(at + o1);
        far2 = Fetch(at - o1);
        far3 = Fetch(at + o2);
        far4 = Fetch(at - o2);
        noise = Hash(seed + float2(91.0f, 13.0f)) - 0.5f;
    }
    // Faux HDR : deux anneaux de 8 échantillons, à un 400e et un 120e de la largeur de la zone (13 et 43 pixels
    // pour un œil de 5 200 pixels), pour garder le même aspect à toutes les résolutions.
    float3 nearRing = e, farRing = e;
    if (p4.z > 0.0f) {
        float r1 = max(2.0f, p4.x / 400.0f), r2 = max(6.0f, p4.x / 120.0f);
        float d1 = r1 * 0.7071f, d2 = r2 * 0.7071f;
        nearRing = (Fetch(at + int2(int(r1), 0)) + Fetch(at - int2(int(r1), 0)) + Fetch(at + int2(0, int(r1))) + Fetch(at - int2(0, int(r1))) +
                    Fetch(at + int2(int(d1), int(d1))) + Fetch(at - int2(int(d1), int(d1))) + Fetch(at + int2(int(d1), -int(d1))) +
                    Fetch(at + int2(-int(d1), int(d1)))) * 0.125f;
        farRing = (Fetch(at + int2(int(r2), 0)) + Fetch(at - int2(int(r2), 0)) + Fetch(at + int2(0, int(r2))) + Fetch(at - int2(0, int(r2))) +
                   Fetch(at + int2(int(d2), int(d2))) + Fetch(at - int2(int(d2), int(d2))) + Fetch(at + int2(int(d2), -int(d2))) +
                   Fetch(at + int2(-int(d2), int(d2)))) * 0.125f;
    }
    float3 color = FilterPixel(a, b, c, d, e, f, g, h, i, far1, far2, far3, far4, noise, nearRing, farRing, p0.x, p0.y, p5.x, p0.z, p4.z, p0.w, p1.x,
                               p1.y, p1.z, p1.w, p2.x, p2.y, p2.z);
    if (p3.w > 0.5f) color = ToLinear(color);
    float alpha = source.Load(int3(clamp(at, int2(0, 0), int2(p4.xy) - int2(1, 1)), 0)).a;
    return float4(color, alpha);
}
