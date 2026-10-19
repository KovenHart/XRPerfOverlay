// Outil de mise au point : dessine le panneau dans des fichiers PPM (sans VR ni carte graphique)
// et vérifie le calcul des statistiques. Compilation : voir CMake, option XRPO_BUILD_TESTS.
#undef NDEBUG  // les assert doivent rester actifs, même en configuration Release
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../layer/src/ascii.h"
#include "../layer/src/frame_history.h"
#include "../layer/src/frame_timer.h"
#include "../layer/src/output_tap.h"
#include "../layer/src/pose_math.h"
#include "../layer/src/view_pose.h"
#include "../layer/src/window_pixels.h"
#include "../layer/src/vr_matrix.h"
#include "../layer/src/panel.h"
#include "../layer/src/preview_api.h"
#include "../layer/src/report_control.h"
#include "../layer/src/session_stats.h"
#include "../layer/src/settings_apply.h"
#include "../layer/src/stats.h"

using namespace xrpo;

// Compose le panneau (alpha prémultiplié) sur un fond uni et l'écrit en PPM.
// `height` : nombre de lignes écrites (hauteur utile du panneau).
static void writePpm(const Panel& p, const std::string& path, int bgR, int bgG, int bgB, int height = Panel::kBodyHeight) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", Panel::kWidth, height);
    const uint8_t* px = p.pixels();
    for (int i = 0; i < Panel::kWidth * height; ++i) {
        const int inv = 255 - px[i * 4 + 3];
        const unsigned char rgb[3] = {static_cast<unsigned char>(px[i * 4 + 0] + bgR * inv / 255),
                                      static_cast<unsigned char>(px[i * 4 + 1] + bgG * inv / 255),
                                      static_cast<unsigned char>(px[i * 4 + 2] + bgB * inv / 255)};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}

// Image RGBA prémultipliée quelconque posée sur un fond uni.
static void writeRgbaPpm(const uint8_t* px, int width, int height, const std::string& path, int bgR, int bgG, int bgB) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", width, height);
    for (int i = 0; i < width * height; ++i) {
        const int inv = 255 - px[i * 4 + 3];
        const unsigned char rgb[3] = {static_cast<unsigned char>(px[i * 4 + 0] + bgR * inv / 255),
                                      static_cast<unsigned char>(px[i * 4 + 1] + bgG * inv / 255),
                                      static_cast<unsigned char>(px[i * 4 + 2] + bgB * inv / 255)};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}

static bool near3(Vec3 a, Vec3 b) { return std::fabs(a.x - b.x) < 1e-5f && std::fabs(a.y - b.y) < 1e-5f && std::fabs(a.z - b.z) < 1e-5f; }

// Conversions pose <-> matrice OpenVR, inverse d'une pose, placement devant les yeux.
static void testVrMath() {
    const Pose poses[] = {{{0.1f, -0.2f, 0.3f}, {}},
                          {{1.f, 2.f, -3.f}, {0.2705981f, 0.2705981f, 0.6532815f, 0.6532815f}},
                          {{-0.4f, 0.f, 0.05f}, {0.f, 1.f, 0.f, 0.f}},
                          {{0.f, 0.f, 0.f}, {-0.5f, 0.5f, -0.5f, 0.5f}}};
    for (const Pose& p : poses) {
        float m[3][4];
        PoseToMatrix34(p, m);
        // La matrice transforme un point comme la pose.
        const Vec3 v = {0.3f, -0.7f, 1.1f};
        const Vec3 byPose = p.position + Rotate(p.orientation, v);
        const Vec3 byMatrix = {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3], m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3],
                               m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3]};
        assert(near3(byPose, byMatrix));
        const Pose back = Matrix34ToPose(m);
        assert(near3(back.position, p.position) && near3(Rotate(back.orientation, v), Rotate(p.orientation, v)));
        // Pose puis son inverse : identité.
        const Pose id = Compose(p, Inverse(p));
        assert(near3(id.position, {0, 0, 0}) && near3(Rotate(id.orientation, v), v));
    }
    // Panneau devant les yeux : sa normale (+Z) pointe vers les yeux, pour chaque emplacement.
    Config c;
    for (int corner = 0; corner < static_cast<int>(Corner::Count); ++corner) {
        c.position = static_cast<Corner>(corner);
        const Pose p = PanelViewPose(c);
        const Vec3 normal = Rotate(p.orientation, {0, 0, 1});
        const float length = Length(p.position);
        assert(near3(normal, p.position * (-1.f / length)) && std::fabs(p.position.z + c.distance) < 1e-6f);
    }
    c.faceViewer = false;
    assert(std::fabs(PanelViewPose(c).orientation.w - 1.f) < 1e-6f);
    const Pose w = WatchViewPose();
    assert(near3(Rotate(w.orientation, {0, 0, 1}), w.position * (-1.f / Length(w.position))));
    std::printf("tests des calculs OpenVR : OK\n");
}

static void testPoseMath() {
    const Vec3 X = {1, 0, 0}, Y = {0, 1, 0}, Z = {0, 0, 1};
    const Quat id = FromBasis(X, Y, Z);
    assert(std::fabs(id.w - 1.f) < 1e-6f && near3(Rotate(id, {0.3f, -2.f, 5.f}), {0.3f, -2.f, 5.f}));
    // Toutes les orientations : les axes du calque sont bien ceux demandés (repère direct).
    for (int hand = 0; hand < 2; ++hand) {
        for (int placement = 0; placement < static_cast<int>(HandPlacement::Count); ++placement) {
            const Pose p = HandPlacementPose(static_cast<Hand>(hand), static_cast<HandPlacement>(placement));
            const Vec3 x = Rotate(p.orientation, X), y = Rotate(p.orientation, Y), z = Rotate(p.orientation, Z);
            assert(std::fabs(Dot(x, y)) < 1e-5f && std::fabs(Dot(y, z)) < 1e-5f && near3(Cross(x, y), z));
        }
    }
    // Dos du poignet gauche : face vers -X de la poignée (dos de la main), haut opposé au pouce, droite vers les doigts.
    Pose p = HandPlacementPose(Hand::Left, HandPlacement::WristBack);
    assert(near3(Rotate(p.orientation, Z), {-1, 0, 0}) && near3(Rotate(p.orientation, Y), {0, -1, 0}) && near3(Rotate(p.orientation, X), {0, 0, -1}));
    assert(p.position.z > 0.08f && p.position.x < -0.03f);   // vers le coude, à la surface du dos de la main
    p = HandPlacementPose(Hand::Right, HandPlacement::WristBack);
    assert(near3(Rotate(p.orientation, Z), {1, 0, 0}) && p.position.x > 0.03f);
    p = HandPlacementPose(Hand::Left, HandPlacement::WristInside);
    assert(near3(Rotate(p.orientation, Z), {1, 0, 0}) && near3(Rotate(p.orientation, Y), {0, 1, 0}));
    p = HandPlacementPose(Hand::Right, HandPlacement::Above, 0.01f, 0.02f);
    assert(Rotate(p.orientation, Z).z > 0.8f && Rotate(p.orientation, Z).y > 0.4f && std::fabs(p.position.y - 0.09f) < 1e-6f && std::fabs(p.position.z - 0.03f) < 1e-6f);
    // Composition : le calque d'une main posée devant les yeux.
    const Pose hand = {{0.f, -0.2f, -0.4f}, FromBasis(X, Y, Z)};
    assert(near3(Compose(hand, {{0.f, 0.f, 0.1f}, id}).position, {0.f, -0.2f, -0.3f}));

    // Regard : face aux yeux et dans l'axe -> vu ; retourné, ou très sur le côté -> non.
    assert(IsLookedAt({{0.f, -0.1f, -0.35f}, id}, false));
    assert(!IsLookedAt({{0.f, -0.1f, -0.35f}, {0.f, 1.f, 0.f, 0.f}}, true));
    assert(!IsLookedAt({{0.6f, 0.f, -0.3f}, id}, true));
    assert(!IsLookedAt({{0.f, 0.f, -2.f}, id}, true));   // trop loin pour être un poignet
    // Hystérésis : légèrement tourné (environ 60°), reste affiché s'il l'était, ne s'affiche pas sinon.
    const float a = 0.55f;  // demi-angle de la rotation autour de Y
    const Quat tilted = {0.f, std::sin(a), 0.f, std::cos(a)};
    assert(IsLookedAt({{0.f, 0.f, -0.35f}, tilted}, true) && !IsLookedAt({{0.f, 0.f, -0.35f}, tilted}, false));
    std::printf("tests des positions sur la main : OK\n");
}

static void testStats() {
    const float nan = std::nanf("");
    FrameStats st;
    st.setWindow(10.0);
    double t = 100.0;
    // 3 s à 90 FPS, puis 2 s à 45 FPS.
    for (int i = 0; i < 270; ++i, t += 1.0 / 90.0) {
        st.onFrame(t, 5.0f);
        if (i % 20 == 0) st.snapshot(t);
    }
    StatsSnapshot s = st.snapshot(t - 1.0 / 90.0);
    assert(s.valid && std::fabs(s.fps - 90.f) < 0.5f && std::fabs(s.cpuMs - 5.f) < 0.01f);
    for (int i = 0; i < 90; ++i, t += 1.0 / 45.0) {
        st.onFrame(t, 12.0f);
        if (i % 10 == 0) st.snapshot(t);
    }
    s = st.snapshot(t - 1.0 / 45.0);
    assert(s.valid && std::fabs(s.fps - 45.f) < 0.5f);
    assert(std::fabs(s.min - 45.f) < 1.5f && std::fabs(s.max - 90.f) < 1.f);
    assert(std::fabs(s.avg - 360.0f / 5.0f) < 1.0f);  // 360 images en 5 s
    assert(std::fabs(s.cpuMs - 12.f) < 0.01f);
    std::printf("apres 90 puis 45 FPS : fps=%.1f avg=%.1f min=%.1f max=%.1f cpu=%.1f ms\n", s.fps, s.avg, s.min, s.max, s.cpuMs);

    // Le jeu ne soumet plus rien : plus de valeur valide.
    assert(!st.snapshot(t + 2.0).valid);
    // Reprise après une pause : l'historique est oublié, MIN ne garde pas la trace du trou.
    t += 5.0;
    for (int i = 0; i < 180; ++i, t += 1.0 / 90.0) {
        st.onFrame(t, nan);
        if (i % 20 == 0) st.snapshot(t);
    }
    s = st.snapshot(t - 1.0 / 90.0);
    assert(s.valid && std::fabs(s.min - 90.f) < 1.f && std::fabs(s.avg - 90.f) < 1.f && std::isnan(s.cpuMs));

    // La fenêtre glissante oublie les vieilles valeurs.
    for (int i = 0; i < 12 * 60; ++i, t += 1.0 / 60.0) {
        st.onFrame(t, 3.0f);
        if (i % 15 == 0) st.snapshot(t);
    }
    s = st.snapshot(t - 1.0 / 60.0);
    assert(std::fabs(s.max - 60.f) < 1.f && std::fabs(s.avg - 60.f) < 0.5f);
    std::printf("tests des statistiques : OK\n");
}

static void testFrameTimer() {
    FrameTimer t;
    assert(t.targetFps() == 0.f && std::isnan(t.onEndFrame(1.0)));

    // Boucle simple : attente, début, fin.
    t.onWaitFrame(10.000, 13888888); t.onBeginFrame();
    assert(std::fabs(t.onEndFrame(10.006) - 6.f) < 0.01f);
    assert(std::isnan(t.onEndFrame(10.007)));  // un second xrEndFrame sans nouvelle image ne mesure rien

    // Moteur en pipeline : l'attente de l'image suivante arrive avant la fin de l'image en cours.
    t.onWaitFrame(11.000, 13888888); t.onBeginFrame();
    t.onWaitFrame(11.010, 13888888);
    assert(std::fabs(t.onEndFrame(11.012) - 12.f) < 0.01f);
    t.onBeginFrame();
    assert(std::fabs(t.onEndFrame(11.020) - 10.f) < 0.01f);

    // Images jamais soumises a travers nous (vides, abandonnees) : aucun decalage ne doit s'installer.
    for (int i = 0; i < 50; ++i) { t.onWaitFrame(12.0 + i * 0.0278, 27777777); t.onBeginFrame(); }
    t.onWaitFrame(20.000, 27777777); t.onBeginFrame();
    assert(std::fabs(t.onEndFrame(20.005) - 5.f) < 0.01f);

    // La cible suit la cadence annoncee : 36 quand le jeu est bride a la moitie de 72 Hz (framegen).
    assert(std::fabs(t.targetFps() - 36.f) < 0.01f);
    // Valeurs aberrantes ignorees.
    t.onWaitFrame(21.0, 0); t.onWaitFrame(21.0, 1000); t.onWaitFrame(21.0, 900000000);
    assert(std::fabs(t.targetFps() - 36.f) < 0.01f);
    t.reset();
    assert(t.targetFps() == 0.f);
    std::printf("tests du chronometrage : OK\n");
}

static void writeFile(const std::string& path, const std::string& text) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f) { std::fwrite(text.data(), 1, text.size(), f); std::fclose(f); }
}

static bool has(const std::string& text, const char* part) {
    if (text.find(part) != std::string::npos) return true;
    std::printf("ABSENT : [%s]\n", part);
    return false;
}

static void testSessionStats(const std::string& dir) {
    SessionStats st;
    PanelData d;
    d.fpsValid = true; d.targetFps = 36.f; d.sensorsOk = true;
    d.cpuLoad = 35.f; d.cpuTemp = 57.f; d.cpuMHz = 5040.f; d.gpuLoad = 73.f; d.gpuTemp = 61.f; d.gpuMHz = 2857.f;
    d.vramUsedMB = 23.4f * 1024.f; d.vramTotalMB = 31.8f * 1024.f;

    // 10 minutes a 36 FPS, avec 2 % d'images a 50 ms et une coupure de 8 s (chargement) au milieu.
    double t = 1000.0;
    int frame = 0;
    for (int second = 0; second < 600; ++second) {
        if (second == 300) t += 8.0;
        for (int i = 0; i < 36; ++i, ++frame) {
            const bool slow = frame % 50 == 0;
            t += slow ? 0.050 : 1.0 / 36.0;
            st.onFrame(t, slow ? 30.f : 15.6f);
        }
        for (int k = 0; k < 4; ++k) {
            d.fps = second % 60 == 59 ? 31.f : 36.f;  // une seconde plus lente par minute
            d.gpuFrameEstimateMs = d.gpuFrameMs = d.gpuLoad / 100.f * 1000.f / d.fps;
            d.gpuTemp = 61.f + static_cast<float>(second % 7);
            st.onSample(d, true, t);
        }
    }
    // 2 % des images durent 50 ms : le "1 % low" tombe dessus (20 FPS), le reste est a 36.
    assert(std::fabs(st.slowFrameMs(0.01) - 50.f) < 0.1f);
    assert(std::fabs(st.slowFrameMs(0.05) - 27.8f) < 0.1f);
    assert(st.activeSeconds() > 600.0 && st.activeSeconds() < 612.0);  // la coupure de 8 s n'est pas comptee
    assert(std::fabs(st.shareAtMost(1000.f / 36.f * 1.05f) - 0.98) < 0.001);
    SessionStats empty;
    assert(std::isnan(empty.slowFrameMs(0.01)) && std::isnan(empty.shareAtMost(10.f)));

    SessionInfo info;
    info.game = "Witcher3.exe"; info.api = "Direct3D 12"; info.startText = "2026-10-10 06:50:12"; info.nowText = "2026-10-10 07:00:28";
    info.finished = true; info.wallSeconds = 616.0;
    // Casque et resolution inconnus : des tirets, pas de ligne vide.
    std::string text = st.format(info);
    assert(text.find("XR Perf Overlay - r\xC3\xA9sum\xC3\xA9 de session\r\n") == 3);   // version non indiquee
    { SessionInfo vi = info; vi.version = "1.2.3"; assert(st.format(vi).find("XR Perf Overlay 1.2.3 - r\xC3\xA9sum\xC3\xA9 de session\r\n") == 3); }
    assert(has(text, "Casque           : --\r\n") && has(text, "Runtime OpenXR   : --\r\n") && has(text, "il    : --\r\n"));
    d.renderWidth = 3168; d.renderHeight = 3168; st.onSample(d, false, t);
    info.headset = "Pimax Crystal Light"; info.system = "Pimax Crystal Light"; info.runtime = "PimaxXR 0.4.5";
    text = st.format(info);
    assert(text.find("Processeur  ") == std::string::npos && text.find("Carte graphique  :") == std::string::npos);  // option desactivee
    { SessionInfo hw = info; hw.cpu = "AMD Ryzen 7 9800X3D 8-Core Processor"; hw.gpu = "";
      const std::string h = st.format(hw);
      assert(has(h, "Runtime OpenXR   : PimaxXR 0.4.5\r\nProcesseur       : AMD Ryzen 7 9800X3D 8-Core Processor\r\nCarte graphique  : --\r\nRendu")); }
    assert(has(text, "Casque           : Pimax Crystal Light\r\n") && has(text, "Runtime OpenXR   : PimaxXR 0.4.5\r\n"));
    assert(has(text, "il    : 3168 x 3168\r\n"));
    // Nom choisi dans le .ini different de celui du runtime, resolution changee en cours de session.
    d.renderWidth = 2244; d.renderHeight = 2640; st.onSample(d, false, t);
    d.renderWidth = 2640; d.renderHeight = 3104; st.onSample(d, false, t);
    info.system = "SteamVR/OpenXR : aapvr";
    text = st.format(info);
    assert(has(text, "Casque           : Pimax Crystal Light   (nom annonc\xC3\xA9 par le runtime : SteamVR/OpenXR : aapvr)\r\n"));
    assert(has(text, "il    : 2640 x 3104   (a vari\xC3\xA9 de 2244 x 2640 \xC3\xA0 3168 x 3168)\r\n"));
    assert(text.find("Rendu recommand") == std::string::npos);   // inconnue : pas de ligne
    { SessionInfo ri = info; ri.recommendedWidth = 2200; ri.recommendedHeight = 2586;
      assert(has(st.format(ri), "il    : 2640 x 3104   (a vari\xC3\xA9 de 2244 x 2640 \xC3\xA0 3168 x 3168)\r\nRendu recommand\xC3\xA9 : 2200 x 2586   (par le runtime ; le jeu rend \xC3\xA0 120 % en largeur)\r\n")); }
    assert(text.size() > 800 && std::strlen(text.c_str()) == text.size() && text.compare(0, 3, "\xEF\xBB\xBF") == 0);
    assert(has(text, "1 % low        : 20.0") && has(text, "Minimum        : 31") && has(text, "Coupures       : 1 "));
    assert(has(text, "tenue 98 % du temps") && has(text, "sur 31.8"));
    // Repartition : 98 % des images dans le budget, 2 % a 50 ms (de 1,5 a 2 fois 27,8 ms).
    assert(has(text, "(budget d'une image pour tenir 36 : 27.8 ms)") && has(text, "  Dans le budget          :  98.0 %\r\n"));
    assert(has(text, "  De 1,5 \xC3\xA0 2 x le budget  :   2.0 %\r\n") && has(text, "  Plus de 2 x le budget   :   0.0 %\r\n"));
    // Releve minute par minute : 11 minutes (la coupure de 8 s decale la fin), FPS d'une minute pleine a ~35,6, GPU estime.
    assert(has(text, "RELEV\xC3\x89 MINUTE PAR MINUTE   (GPU ms : estim\xC3\xA9)\r\n"));
    assert(has(text, "  minute  FPS moy  FPS min  CPU ms  GPU ms  CPU %  GPU %  CPU \xC2\xB0" "C  GPU \xC2\xB0" "C  VRAM Go\r\n"));
    assert(has(text, "       1     35.4       36    15.9    20.3     35     73      57      64     23.4\r\n"));
    assert(has(text, "      11     35.5       31    15.9    20.6     35     73      57      64     23.4\r\n") && text.find("      12  ") == std::string::npos);
    assert(text.find("GPU (mesur") == std::string::npos);
    writeFile(dir + "/resume_exemple.txt", text);

    // Avec la mesure du temps GPU : ligne "GPU (mesure)", releve par minute sur les mesures.
    for (int i = 0; i < 2000; ++i) st.onGpuFrame(1000.0 + i * 0.02, 18.5f);
    text = st.format(info);
    assert(has(text, "  GPU (mesur\xC3\xA9)               18.5      18.5\r\n") && has(text, "  GPU (estim\xC3\xA9)"));
    assert(has(text, "Le temps GPU mesur\xC3\xA9 est l'\xC3\xA9" "cart"));
    // Mesures sur la premiere minute seulement : les suivantes reprennent l'estimation, marquee "~".
    assert(has(text, "(GPU ms : mesur\xC3\xA9 ; ~ = estim\xC3\xA9, minute sans mesure)\r\n"));
    assert(has(text, "       1     35.4       36    15.9    18.5     35     73      57      64     23.4\r\n"));
    assert(has(text, "      11     35.5       31    15.9   ~20.6     35     73      57      64     23.4\r\n"));
    writeFile(dir + "/resume_exemple_mixte.txt", text);

    // Mesure ecartee (non representative) : plus de ligne ni de releve "mesure", une explication.
    {
        SessionStats rj = st;
        rj.rejectGpuMeasure(0.2f, 12.4f, 88.f);
        rj.onGpuFrame(t, 0.3f);   // ignoree
        const std::string r = rj.format(info);
        assert(r.find("GPU (mesur") == std::string::npos && r.find("~") == std::string::npos && has(r, "(GPU ms : estim\xC3\xA9)\r\n"));
        assert(has(r, "       1     35.4       36    15.9    20.3     35     73      57      64     23.4\r\n"));
        assert(has(r, "Mesure r\xC3\xA9" "elle du temps GPU \xC3\xA9" "cart\xC3\xA9" "e : 0.2 ms mesur\xC3\xA9 contre 12.4 ms estim\xC3\xA9, carte charg\xC3\xA9" "e \xC3\xA0 88 %."));
        assert(r.find("Le temps GPU mesur") == std::string::npos);
        writeFile(dir + "/resume_exemple_ecarte.txt", r);
    }

    // Session sans capteurs ni images : des tirets, aucune section vide.
    const std::string e = empty.format(SessionInfo());
    assert(has(e, "Capteurs indisponibles") && has(e, "Moyenne        : --") && e.find("RELEV") == std::string::npos && e.find("PARTITION") == std::string::npos);

    // Session de plus de 24 h : le releve s'arrete a 24 h sans erreur.
    SessionStats longOne;
    for (int minute = 0; minute < 24 * 60 + 30; ++minute) { longOne.onFrame(minute * 60.0, 5.f); longOne.onFrame(minute * 60.0 + 0.011, 5.f); longOne.onSample(d, true, minute * 60.0); }
    const std::string l = longOne.format(SessionInfo());
    assert(has(l, "    1440  ") && l.find("    1441  ") == std::string::npos);

    // Exemple plus parlant pour la fenetre des rapports : jeu bride a 36 avec generation d'images, qui
    // ralentit en fin de session (la carte chauffe), mesure du temps GPU active.
    SessionStats demo;
    PanelData g = d;
    double now = 5000.0;
    for (int second = 0; second < 25 * 60; ++second) {
        const double heat = second > 15 * 60 ? (second - 15 * 60) / 600.0 : 0.0;   // de 0 a 1 sur les dix dernieres minutes
        const double frameMs = 1000.0 / 36.0 * (1.0 + 0.25 * heat);
        const int frames = static_cast<int>(1000.0 / frameMs);
        for (int i = 0; i < frames; ++i) {
            const bool hitch = (second * 7 + i) % 211 == 0;
            now += hitch ? frameMs * 2.2 / 1000.0 : frameMs / 1000.0;
            demo.onFrame(now, static_cast<float>(14.0 + 2.0 * heat + (hitch ? 12.0 : 0.0)));
            demo.onGpuFrame(now, static_cast<float>(19.0 + 7.0 * heat + (i % 5) * 0.3));
        }
        for (int k = 0; k < 4; ++k) {
            g.fps = static_cast<float>(frames); g.targetFps = 36.f; g.outFps = 72.f;
            g.gpuLoad = static_cast<float>(76.0 + 20.0 * heat); g.gpuTemp = static_cast<float>(64.0 + 19.0 * heat); g.cpuTemp = static_cast<float>(58.0 + 4.0 * heat);
            g.cpuLoad = 31.f; g.vramUsedMB = static_cast<float>((21.0 + 3.0 * heat) * 1024.0);
            g.gpuFrameEstimateMs = g.gpuLoad / 100.f * 1000.f / g.fps;
            demo.onSample(g, true, now);
        }
    }
    SessionInfo di = info;
    di.game = "DeusExHRVRHost.exe"; di.api = "Direct3D 11"; di.version = "1.0.0";
    di.cpu = "AMD Ryzen 7 9800X3D 8-Core Processor"; di.gpu = "NVIDIA GeForce RTX 5090"; di.startText = "2026-10-11 21:05:40"; di.nowText = "2026-10-11 21:30:52"; di.wallSeconds = 1512.0;
    writeFile(dir + "/resume_exemple_mesure.txt", demo.format(di));
    std::printf("tests du resume de session : OK\n");
}

static void testRateMeter() {
    RateMeter m;
    assert(std::isnan(m.rate()));
    // Compteur a 72 par seconde, releve toutes les 250 ms.
    uint64_t count = 1000;
    double t = 50.0;
    for (int i = 0; i < 3; ++i, t += 0.25, count += 18) m.add(t, count);
    assert(std::isnan(m.rate()));  // moins d'une seconde de recul
    for (int i = 0; i < 20; ++i, t += 0.25, count += 18) m.add(t, count);
    assert(std::fabs(m.rate() - 72.f) < 0.01f);
    // Releves irreguliers, puis coupure : on repart de zero plutot que d'afficher un debit faux.
    m.add(t + 0.1, count + 7);
    assert(m.rate() > 60.f && m.rate() < 80.f);
    m.add(t + 5.0, count + 20);
    assert(std::isnan(m.rate()));
    // 71 ou 73 mesures pour un casque a 72 Hz : on affiche 72 ; une vraie perte reste visible.
    assert(SnapToRefresh(71.f, 72.f) == 72.f && SnapToRefresh(73.4f, 72.f) == 72.f && SnapToRefresh(68.f, 72.f) == 68.f);
    assert(SnapToRefresh(71.f, 0.f) == 71.f && std::isnan(SnapToRefresh(std::nanf(""), 72.f)));
    // Suivi des swapchains : une soumission n'est nouvelle que si une image a ete rendue entre-temps.
    SwapchainTracker tr;
    assert(!tr.consume(11));                 // jamais vue
    tr.onRelease(11); tr.onRelease(22);      // deux images pretes sur deux swapchains differentes
    assert(tr.consume(11) && !tr.consume(11));   // 1re soumission nouvelle, repetition ensuite
    assert(tr.consume(22) && !tr.consume(22));   // la seconde n'a pas ete "consommee" par la premiere
    for (uint64_t h = 100; h < 300; ++h) tr.onRelease(h);   // bien plus de swapchains que de places
    assert(tr.consume(299) && !tr.consume(299));
    tr.onRelease(0); assert(!tr.consume(0));
    std::printf("tests du debit en sortie : OK\n");
}

static void testAsciiText() {
    // Nom UTF-8 venant du runtime : accents et controles retires, espaces reduits, longueur bornee.
    const char raw[] = "  Pimax\tCrystal   Light \xC3\xA9\xE2\x84\xA2 ";
    assert(AsciiText(raw, sizeof(raw)) == "Pimax Crystal Light");
    assert(AsciiText(L"Casque \u00e0 moi", 64) == "Casque moi");
    assert(AsciiText("abcdef", 3) == "abc");                      // tampon sans zero final
    assert(AsciiText("abcdef", 16, 4) == "abcd");
    assert(AsciiText(static_cast<const char*>(nullptr), 8).empty() && AsciiText("", 1).empty() && AsciiText("   ", 4).empty());
    // Noms de modeles tels que Windows les donne.
    assert(ModelName(AsciiText(L"13th Gen Intel(R) Core(TM) i9-13900K  ", 64, 96)) == "13th Gen Intel Core i9-13900K");
    assert(ModelName(AsciiText(L"AMD Ryzen 7 9800X3D 8-Core Processor           ", 64, 96)) == "AMD Ryzen 7 9800X3D 8-Core Processor");
    assert(ModelName(AsciiText(L"NVIDIA GeForce RTX 5090", 64, 96)) == "NVIDIA GeForce RTX 5090");
    assert(ModelName(AsciiText(L"AMD Radeon\u00ae RX 9070 XT", 64, 96)) == "AMD Radeon RX 9070 XT");
    assert(ModelName("(R)(TM)").empty() && ModelName("").empty());
    std::printf("tests du nettoyage de texte : OK\n");
}

// La ligne casque / resolution ne change rien au reste du panneau, et sans elle le bas reste vide.
static void testInfoLine() {
    PanelData d;
    d.fpsValid = true; d.fps = 90.f; d.fpsAvg = 90.f; d.fpsMin = 90.f; d.fpsMax = 90.f; d.targetFps = 90.f; d.sensorsOk = true;
    Panel plain, withInfo;
    plain.render(d, 0.82f);
    d.showInfo = true; d.renderWidth = 3168; d.renderHeight = 3168;
    std::snprintf(d.headset, sizeof(d.headset), "Pimax Crystal Light");
    withInfo.render(d, 0.82f);
    const size_t row = static_cast<size_t>(Panel::kWidth) * 4;
    // Au-dessus du separateur (hors coins arrondis du bas), les deux panneaux sont identiques.
    assert(!std::memcmp(plain.pixels(), withInfo.pixels(), row * 230));
    bool emptyBelow = true, drawnBelow = false;
    for (size_t i = row * Panel::kBodyHeight; i < row * Panel::kHeight; ++i) {
        emptyBelow = emptyBelow && plain.pixels()[i] == 0;
        drawnBelow = drawnBelow || withInfo.pixels()[i] != 0;
    }
    assert(emptyBelow && drawnBelow);
    // Le meme objet repasse d'un mode a l'autre sans garder de trace.
    withInfo.render(d, 0.82f);
    d.showInfo = false;
    withInfo.render(d, 0.82f);
    assert(!std::memcmp(plain.pixels(), withInfo.pixels(), row * Panel::kHeight));
    // Echelle de rendu propre au jeu (x1.20) : affichee a cote de la resolution, qui s'allonge vers la gauche.
    {
        PanelData base = d; base.showInfo = true; Panel reference; reference.render(base, 0.82f);
        Panel scaled; PanelData sd = base; sd.recommendedWidth = 2640; sd.recommendedHeight = 2640; scaled.render(sd, 0.82f);
        PanelData same = base; same.recommendedWidth = 3150; same.recommendedHeight = 3150; Panel near; near.render(same, 0.82f);
        assert(!std::memcmp(near.pixels(), reference.pixels(), row * Panel::kHeight));   // ecart < 3 % : rien de plus
        assert(std::memcmp(scaled.pixels(), reference.pixels(), row * Panel::kHeight) != 0);
        sd.recommendedWidth = -4; scaled.render(sd, 0.82f);
        assert(!std::memcmp(scaled.pixels(), reference.pixels(), row * Panel::kHeight));  // recommandation absurde : ignoree
    }
    // Nom trop long, tampon du nom plein (sans zero final) et resolution absurde : rien ne deborde.
    d.showInfo = true; d.renderWidth = 123456789; d.renderHeight = -5; d.recommendedWidth = 1;
    std::memset(d.headset, 'W', sizeof(d.headset));
    withInfo.render(d, 0.82f);
    std::printf("tests de la ligne casque / resolution : OK\n");
}

static void testFrameHistory() {
    FrameHistory h;
    float frame[FrameHistory::kColumns], cpu[FrameHistory::kColumns];
    const int last = FrameHistory::kColumns - 1;
    h.columns(100.0, frame, cpu);
    for (float v : frame) assert(v == 0.f);

    // 5 s a 90 images par seconde : toutes les tranches recentes valent 11,1 ms, les plus anciennes sont vides.
    double t = 100.0;
    for (int i = 0; i < 450; ++i, t += 1.0 / 90.0) h.onFrame(t, 4.f);
    t -= 1.0 / 90.0;
    h.columns(t, frame, cpu);
    assert(std::fabs(frame[last] - 11.11f) < 0.05f && std::fabs(cpu[last] - 4.f) < 1e-3f);
    assert(std::fabs(frame[last - 140] - 11.11f) < 0.05f && frame[last - 160] == 0.f && frame[0] == 0.f);
    // Le meme instant releve deux fois donne la meme courbe ; un peu plus tard, elle a seulement glisse.
    float again[FrameHistory::kColumns], later[FrameHistory::kColumns];
    h.columns(t, again, cpu);
    assert(!std::memcmp(frame, again, sizeof(frame)));
    h.columns(t + 5 * FrameHistory::kSliceSeconds, later, cpu);
    for (int i = 5; i < FrameHistory::kColumns; ++i) assert(later[i - 5] == frame[i]);
    for (int i = 0; i < 5; ++i) assert(later[last - i] == 0.f);

    // Une image de 100 ms forme un palier de trois tranches a 100 ms, pas un point isole.
    t += 0.100;
    h.onFrame(t, 30.f);
    h.columns(t, frame, cpu);
    assert(std::fabs(frame[last] - 100.f) < 0.1f && std::fabs(frame[last - 1] - 100.f) < 0.1f && std::fabs(frame[last - 2] - 100.f) < 0.1f);
    assert(std::fabs(cpu[last] - 30.f) < 1e-3f && frame[last - 4] < 12.f);

    // Coupure de plus d'une seconde (chargement) : un trou, et la courbe reprend ensuite.
    t += 3.0;
    h.onFrame(t, 4.f);
    for (int i = 0; i < 9; ++i) { t += 1.0 / 90.0; h.onFrame(t, 4.f); }
    h.columns(t, frame, cpu);
    assert(frame[last] > 11.f && frame[last] < 11.3f && frame[last - 30] == 0.f && frame[last - 80] == 0.f);
    assert(std::fabs(frame[last - 95] - 100.f) < 0.1f);   // l'a-coup d'avant la coupure est toujours la, 3,1 s plus tot

    // Tres longue absence (plus que la fenetre), valeurs absurdes, remise a zero.
    t += 60.0;
    h.onFrame(t, std::nanf(""));
    t += 0.02; h.onFrame(t, std::nanf(""));
    h.columns(t, frame, cpu);
    assert(std::fabs(frame[last] - 20.f) < 0.1f && cpu[last] == 0.f && frame[last - 2] == 0.f && frame[0] == 0.f);
    h.onFrame(t - 5.0, 1.f);   // horloge qui recule : ignore sans rien casser
    h.reset();
    h.columns(t, frame, cpu);
    for (float v : frame) assert(v == 0.f);

    // Courbe GPU estimee : un releve toutes les 3 s (refresh_ms = 3000) couvre les 3 s ecoulees,
    // mais jamais une coupure sans image.
    {
        FrameHistory g;
        float gpu[FrameHistory::kColumns];
        double u = 200.0;
        for (int i = 0; i < 270; ++i, u += 1.0 / 90.0) g.onFrame(u, 4.f);   // 3 s d'images
        u -= 1.0 / 90.0;
        g.onGpuEstimate(u - 3.0, u, 8.f);
        g.columns(u, frame, cpu, gpu);
        assert(gpu[last] == 8.f && gpu[last - 80] == 8.f && gpu[last - 95] == 0.f);
        // Coupure de 2 s puis 1 s d'images : le releve suivant ne marque que les tranches avec images.
        u += 2.0;
        g.onFrame(u, 4.f);
        for (int i = 0; i < 90; ++i) { u += 1.0 / 90.0; g.onFrame(u, 4.f); }
        g.onGpuEstimate(u - 3.0, u, 9.f);
        g.columns(u, frame, cpu, gpu);
        assert(gpu[last] == 9.f && gpu[last - 25] == 9.f && gpu[last - 45] == 0.f && frame[last - 45] == 0.f);
        // Mesure image par image : rangee a la place de son image.
        g.onGpuFrame(u - 0.5, 11.f);
        g.onGpuFrame(u - 0.5 + 1.0 / 90.0, 12.f);
        g.columns(u, frame, cpu, gpu);
        assert(gpu[last - 16] == 9.f && gpu[last - 15] >= 11.f && std::max(gpu[last - 15], gpu[last - 14]) == 12.f && gpu[last] == 9.f);
    }
    std::printf("tests de l'historique des temps d'image : OK\n");
}

static void testSettings() {
    // Noms du .ini et couleurs.
    assert(NameIndex(kLayoutNames, "graph", 0) == 1 && NameIndex(kLayoutNames, "compact", 0) == 2 && NameIndex(kLayoutNames, "", 1) == 1);
    assert(NameIndex(kThemeNames, "custom", 0) == static_cast<int>(ThemeId::Custom) && NameIndex(kThemeNames, "rose", 3) == 3);
    assert(sizeof(kLayoutNames) / sizeof(kLayoutNames[0]) == static_cast<size_t>(Layout::Count));
    assert(sizeof(kThemeNames) / sizeof(kThemeNames[0]) == static_cast<size_t>(ThemeId::Count));
    Rgb c = {1, 2, 3};
    assert(ParseColor("#0E1116", c) && c.r == 14 && c.g == 17 && c.b == 22);
    assert(ParseColor("ff8000", c) && c.r == 255 && c.g == 128 && c.b == 0);
    assert(!ParseColor("", c) && !ParseColor("#12345", c) && !ParseColor("#12345g", c) && !ParseColor("#1234567", c) && c.r == 255);

    // Bloc de reglages : applique tel quel quand il est sain...
    Config cfg;
    SettingsBlock s = {};
    s.visible = 0; s.position = static_cast<uint32_t>(Corner::TopRight); s.distance = 1.5f; s.offsetX = 0.2f; s.offsetY = 0.1f; s.width = 0.5f;
    s.opacity = 0.4f; s.globalOpacity = 0.6f; s.faceViewer = 0; s.clock = 0; s.headsetInfo = 0; s.layout = 1; s.theme = static_cast<uint32_t>(ThemeId::Custom);
    s.customBackground = 0x102030; s.customText = 0xF0E0D0; s.customAccent = 0xFF8800;
    ApplySettings(cfg, s);
    assert(!cfg.visible && cfg.position == Corner::TopRight && cfg.distance == 1.5f && cfg.offsetX == 0.2f && cfg.width == 0.5f && cfg.opacity == 0.4f);
    assert(cfg.globalOpacity == 0.6f && !cfg.faceViewer && !cfg.clock && !cfg.headsetInfo && cfg.layout == Layout::Graph && cfg.theme == ThemeId::Custom);
    const Theme custom = ThemeOf(cfg);
    assert(custom.background.r == 0x10 && custom.text.g == 0xE0 && custom.accent.r == 0xFF && custom.accent.g == 0x88);
    assert(custom.good.g == PresetTheme(ThemeId::Dark).good.g);               // fond sombre : couleurs d'etat vives
    assert(CustomTheme({250, 250, 250}, {0, 0, 0}, {0, 0, 200}).good.g == PresetTheme(ThemeId::Light).good.g);
    // ... et borne quand il ne l'est pas : rien ne sort des limites du .ini.
    const float nan = std::nanf("");
    s.position = 99; s.layout = 99; s.theme = 99; s.distance = nan; s.width = 50.f; s.opacity = -3.f; s.globalOpacity = 0.f; s.offsetX = nan; s.offsetY = 1e9f;
    ApplySettings(cfg, s);
    assert(cfg.position == Corner::TopRight && cfg.layout == Layout::Graph && cfg.theme == ThemeId::Custom && cfg.distance == 1.5f);
    assert(cfg.width == kMaxWidth && cfg.opacity == 0.f && cfg.globalOpacity == kMinGlobalOpacity && cfg.offsetX == 0.2f && cfg.offsetY == 3.f);
    assert(PresetTheme(static_cast<ThemeId>(42)).background.r == PresetTheme(ThemeId::Dark).background.r);

    // Accroche aux manettes et montre.
    assert(NameIndex(kAnchorNames, "right_hand", 0) == 2 && NameIndex(kPlacementNames, "above", 0) == 2 && NameIndex(kWatchStyleNames, "sport", 0) == 2);
    assert(sizeof(kAnchorNames) / sizeof(kAnchorNames[0]) == static_cast<size_t>(Anchor::Count));
    assert(sizeof(kPlacementNames) / sizeof(kPlacementNames[0]) == static_cast<size_t>(HandPlacement::Count));
    assert(sizeof(kWatchStyleNames) / sizeof(kWatchStyleNames[0]) == static_cast<size_t>(WatchStyle::Count));
    SettingsBlock h = s;
    h.anchor = 1; h.handPlacement = 1; h.gazeOnly = 1; h.handAlong = 0.05f; h.handLift = 0.02f;
    h.watchEnabled = 1; h.watchAnchor = 2; h.watchPlacement = 2; h.watchStyle = 1; h.watchElements = kWatchFps | 0x80; h.watchGazeOnly = 0;
    h.watchSize = 0.08f; h.watchOpacity = 0.7f; h.watchAlong = -0.03f; h.watchLift = 0.01f;
    ApplySettings(cfg, h);
    assert(cfg.anchor == Anchor::LeftHand && cfg.handPlacement == HandPlacement::WristInside && cfg.gazeOnly && cfg.handAlong == 0.05f && cfg.handLift == 0.02f);
    assert(cfg.watch.enabled && cfg.watch.anchor == Anchor::RightHand && cfg.watch.placement == HandPlacement::Above && cfg.watch.style == WatchStyle::Digital);
    assert(cfg.watch.elements == kWatchFps && !cfg.watch.gazeOnly && cfg.watch.size == 0.08f && cfg.watch.opacity == 0.7f && cfg.watch.along == -0.03f);
    assert(WantsHandTracking(cfg) && !WantsBattery(cfg));
    h.anchor = 9; h.watchStyle = 9; h.watchSize = 5.f; h.watchOpacity = nan; h.handLift = -1.f;
    ApplySettings(cfg, h);
    assert(cfg.anchor == Anchor::LeftHand && cfg.watch.style == WatchStyle::Digital && cfg.watch.size == kMaxWatchSize && cfg.watch.opacity == 0.7f && cfg.handLift == kMinHandLift);
    Config plain;
    assert(!WantsHandTracking(plain) && !WantsBattery(plain));
    plain.watch.enabled = true; plain.watch.anchor = Anchor::View;
    assert(!WantsHandTracking(plain) && WantsBattery(plain));
    std::printf("tests des reglages : OK\n");
}

// Apercu hors jeu : meme image que Panel::render pour les memes donnees, hauteur selon la presentation.
static void testPreviewApi() {
    std::vector<uint8_t> px(static_cast<size_t>(Panel::kWidth) * Panel::kHeight * 4, 0xAB);
    SettingsBlock s = {};
    s.visible = 1; s.position = 5; s.distance = 1.f; s.offsetX = 0.34f; s.offsetY = 0.26f; s.width = 0.36f; s.opacity = 0.82f; s.globalOpacity = 1.f;
    s.faceViewer = 1; s.clock = 1; s.headsetInfo = 1; s.layout = 0; s.theme = 0;
    s.magic = kSettingsBlockMagic; s.version = kSettingsBlockVersion;
    SensorBlock sensors = {};
    sensors.magic = kSharedBlockMagic; sensors.version = kSharedBlockVersion;
    sensors.cpuLoad = 35.f; sensors.cpuTemp = 57.f; sensors.cpuMHz = 5040.f; sensors.gpuLoad = 73.f; sensors.gpuTemp = 61.f; sensors.gpuMHz = 2857.f;
    sensors.vramUsedMB = 23.4f * 1024.f; sensors.vramTotalMB = 31.8f * 1024.f;
    const uint32_t size = static_cast<uint32_t>(px.size());
    // Arguments invalides : rien n'est ecrit.
    assert(xrpoRenderPreview(nullptr, &sensors, 1.0, 600, "x", px.data(), size) == 0);
    assert(xrpoRenderPreview(&s, &sensors, 1.0, 600, "x", nullptr, size) == 0);
    assert(xrpoRenderPreview(&s, &sensors, 1.0, 600, "x", px.data(), size - 1) == 0);
    assert(xrpoRenderPreview(&s, &sensors, std::nan(""), 600, "x", px.data(), size) == 0);
    { SettingsBlock old = s; old.version = 1; assert(xrpoRenderPreview(&old, &sensors, 1.0, 600, "x", px.data(), size) == 0); }  // bloc d'une autre version
    for (uint8_t b : px) assert(b == 0xAB);

    // Aperçu de la montre.
    {
        std::vector<uint8_t> w(static_cast<size_t>(WatchFace::kSize) * WatchFace::kSize * 4, 0xAB);
        const uint32_t wsize = static_cast<uint32_t>(w.size());
        SettingsBlock ws = s; ws.watchStyle = 1; ws.watchElements = kWatchAllElements; ws.watchOpacity = 1.f; ws.watchSize = 0.06f;
        assert(xrpoRenderWatchPreview(&ws, 600, w.data(), wsize - 1) == 0 && xrpoRenderWatchPreview(&ws, -1, w.data(), wsize) == 0);
        assert(xrpoRenderWatchPreview(nullptr, 600, w.data(), wsize) == 0 && w[0] == 0xAB);
        assert(xrpoRenderWatchPreview(&ws, (21 * 60 + 47) * 60 + 23, w.data(), wsize) == WatchFace::kSize);
        WatchData expected; expected.style = WatchStyle::Digital; expected.hour = 21; expected.minute = 47; expected.second = 23;
        expected.sessionSeconds = 1 * 3600 + 23 * 60 + 45; expected.battery[0] = 0.8f; expected.battery[1] = 0.35f;
        expected.fpsValid = true; expected.fps = 89.f; expected.targetFps = 90.f;
        WatchFace reference; reference.render(expected, 1.f);
        assert(!std::memcmp(w.data(), reference.pixels(), w.size()));
    }

    assert(xrpoRenderPreview(&s, &sensors, 12.5, 17 * 60 + 36, "Pimax Crystal Light", px.data(), size) == Panel::contentHeight(Layout::Classic, true, false));
    PanelData d;
    FillPreviewData(d, 12.5);
    d.sensorsOk = true; d.cpuLoad = 35.f; d.cpuTemp = 57.f; d.cpuMHz = 5040.f; d.gpuLoad = 73.f; d.gpuTemp = 61.f; d.gpuMHz = 2857.f;
    d.vramUsedMB = 23.4f * 1024.f; d.vramTotalMB = 31.8f * 1024.f; d.gpuFrameMs = 0.73f * 1000.f / d.fps;
    d.clockMinutes = 17 * 60 + 36; d.showInfo = true; std::snprintf(d.headset, sizeof(d.headset), "Pimax Crystal Light");
    Panel ref;
    ref.render(d, 1.f, 1.f);
    assert(!std::memcmp(px.data(), ref.pixels(), px.size()));
    assert(px[((static_cast<size_t>(40) * Panel::kWidth) + 320) * 4 + 3] == 255);   // toujours opaque sur le bureau

    // Sans capteurs, sans ligne d'informations, autres presentations ; nom trop long ou absent.
    s.headsetInfo = 0; s.clock = 0;
    assert(xrpoRenderPreview(&s, nullptr, 3.0, 600, nullptr, px.data(), size) == Panel::kBodyHeight);
    s.layout = 1;
    assert(xrpoRenderPreview(&s, &sensors, 3.0, -1, std::string(300, 'N').c_str(), px.data(), size) == Panel::kBodyHeight);
    s.layout = 2; s.headsetInfo = 1;
    assert(xrpoRenderPreview(&s, &sensors, 3.0, 600, "", px.data(), size) == Panel::kCompactHeight + Panel::kInfoHeight);
    SensorBlock stale = sensors; stale.magic = 0;   // bloc de capteurs non initialise : traite comme absent
    s.layout = 99; s.theme = 99; s.width = std::nanf("");
    assert(xrpoRenderPreview(&s, &stale, 1e6, 600, "x", px.data(), size) > 0);
    std::printf("tests de l'apercu hors jeu : OK\n");
}

// Chaque presentation ne dessine rien sous sa hauteur utile, et le decor est refait quand l'apparence change.
static void testLayouts() {
    PanelData d;
    FillPreviewData(d, 42.0);
    d.sensorsOk = true; d.cpuLoad = 35.f; d.cpuTemp = 57.f; d.cpuMHz = 5040.f; d.gpuLoad = 73.f; d.gpuTemp = 61.f; d.gpuMHz = 2857.f;
    d.vramUsedMB = 23.4f * 1024.f; d.vramTotalMB = 31.8f * 1024.f; d.gpuFrameMs = 8.1f; d.clockMinutes = 17 * 60 + 36;
    const size_t row = static_cast<size_t>(Panel::kWidth) * 4;
    Panel p, fresh;
    for (int layout = 0; layout < static_cast<int>(Layout::Count); ++layout) {
        for (int info = 0; info < 8; ++info) {   // ligne casque (bit 0), temps de session (bit 1), batterie (bit 2)
            for (int theme = 0; theme < static_cast<int>(ThemeId::Count); ++theme) {
                d.layout = static_cast<Layout>(layout); d.showInfo = (info & 1) != 0;
                d.statusElements = ((info & 2) ? kWatchSession : 0u) | ((info & 4) ? kWatchBattery : 0u);
                d.sessionSeconds = 4000.0; d.battery[0] = 0.8f; d.battery[1] = (info & 2) ? std::nanf("") : 0.1f; d.charging[1] = true;
                d.theme = theme == static_cast<int>(ThemeId::Custom) ? CustomTheme({250, 244, 230}, {40, 30, 20}, {180, 60, 20}) : PresetTheme(static_cast<ThemeId>(theme));
                p.render(d, 0.82f);
                const int height = Panel::contentHeight(d);
                for (size_t i = row * static_cast<size_t>(height); i < row * Panel::kHeight; ++i) assert(p.pixels()[i] == 0);
                bool lastRowDrawn = false;
                for (size_t i = row * static_cast<size_t>(height - 1); i < row * static_cast<size_t>(height); ++i) lastRowDrawn = lastRowDrawn || p.pixels()[i] != 0;
                assert(lastRowDrawn);
                // Meme image qu'un panneau neuf : rien ne reste de l'apparence precedente.
                Panel clean;
                clean.render(d, 0.82f);
                assert(!std::memcmp(p.pixels(), clean.pixels(), row * Panel::kHeight));
            }
        }
    }
    assert(Panel::contentHeight(Layout::Classic, false, false) == 256 && Panel::contentHeight(Layout::Graph, true, false) == 288 &&
           Panel::contentHeight(Layout::Compact, false, false) == 92 && Panel::contentHeight(Layout::Classic, true, true) == Panel::kHeight);
    // Ligne session / batterie : le corps du panneau est inchangé, la ligne casque descend d'autant.
    {
        PanelData a = d; a.layout = Layout::Classic; a.graphValid = false; a.showInfo = true; a.statusElements = 0;
        PanelData b = a; b.statusElements = kWatchSession | kWatchBattery; b.sessionSeconds = 83.0; b.battery[0] = 0.55f; b.battery[1] = 0.12f;
        Panel pa, pb; pa.render(a, 0.82f); pb.render(b, 0.82f);
        assert(!std::memcmp(pa.pixels(), pb.pixels(), row * (Panel::kBodyHeight - 10)));
        assert(!std::memcmp(pa.pixels() + row * (Panel::kBodyHeight - 4), pb.pixels() + row * (Panel::kBodyHeight + Panel::kStatusHeight - 4), row * (Panel::kInfoHeight - 6)));
        // Batterie inconnue des deux mains (runtime qui ne la donne pas) : la ligne le dit, sans jauges.
        PanelData c = b; c.battery[0] = c.battery[1] = std::nanf(""); Panel pc; pc.render(c, 0.82f);
        assert(std::memcmp(pb.pixels(), pc.pixels(), row * Panel::kHeight) != 0);
    }
    // Courbe absente, valeurs absurdes, cible inconnue : rien ne deborde (verifie par AddressSanitizer).
    d.layout = Layout::Graph; d.graphValid = false; fresh.render(d, 0.82f);
    d.graphValid = true; d.targetFps = 0.f;
    for (int i = 0; i < PanelData::kGraphColumns; ++i) { d.graphFrameMs[i] = i % 3 == 0 ? 1e9f : (i % 3 == 1 ? -5.f : std::nanf("")); d.graphCpuMs[i] = std::nanf(""); }
    fresh.render(d, 0.82f);
    d.targetFps = 1e-6f; fresh.render(d, 0.82f);
    d.sensorsOk = false; d.fpsValid = false; fresh.render(d, 0.82f);
    std::printf("tests des presentations et des couleurs : OK\n");
}

// Fenêtre capturée : pose enregistrée dans le .ini, pose devant soi, sur la main, mise en forme de l'image.
static void testWindow() {
    auto near = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
    // Lecture de la pose « world » du .ini, indépendante des réglages régionaux.
    Pose p;
    assert(ParseWorldPose("0.5000 1.2500 -2.0000 0.0000 0.7071 0.0000 0.7071", p));
    assert(near(p.position.x, 0.5f) && near(p.position.y, 1.25f) && near(p.position.z, -2.f) && near(p.orientation.y, 0.70711f) && near(p.orientation.w, 0.70711f));
    assert(!ParseWorldPose("1 2 3 0 0 0 2", p));  // quaternion de norme 2 : refusé
    assert(ParseWorldPose("  1 2 3 0 0 0 1.2  ", p) && near(p.orientation.w, 1.f));  // presque unitaire : normalisé
    assert(!ParseWorldPose("1 2 3 0 0 0", p) && !ParseWorldPose("", p) && !ParseWorldPose("1,5 2 3 0 0 0 1", p) && !ParseWorldPose("1 2 3 0 0 0 1 x", p));
    assert(!ParseWorldPose("500 0 0 0 0 0 1", p) && ParseWorldPose("-0.25 +1 3. 0 0 0 1", p) && near(p.position.x, -0.25f) && near(p.position.z, 3.f));
    { const char* t = "12.5x"; float v = 0.f; assert(ParseDecimal(t, v) && near(v, 12.5f) && *t == 'x'); const char* u = "-"; assert(!ParseDecimal(u, v)); }
    // Pose devant soi : dans l'axe du regard, face aux yeux, droite même tête penchée.
    Pose head; head.position = {1.f, 1.6f, 0.f};
    const float h = std::sqrt(0.5f);
    head.orientation = {0.f, h, 0.f, h};  // tournée de 90° vers la gauche : regard vers -X
    Pose w = PlaceInFront(head, 0.8f);
    assert(near(w.position.x, 0.2f) && near(w.position.y, 1.6f) && near(w.position.z, 0.f));
    Vec3 face = Rotate(w.orientation, {0.f, 0.f, 1.f}), up = Rotate(w.orientation, {0.f, 1.f, 0.f});
    assert(near(face.x, 1.f) && near(up.y, 1.f));
    const float s = std::sin(0.3f), c = std::cos(0.3f);
    head.orientation = {0.f, 0.f, s, c};  // tête penchée sur le côté (roulis) : la fenêtre reste droite
    w = PlaceInFront(head, 1.f);
    up = Rotate(w.orientation, {0.f, 1.f, 0.f});
    assert(near(w.position.z, -1.f) && near(up.y, 1.f));
    head.orientation = {-h, 0.f, 0.f, h};  // regard vers le bas, à la verticale : gauche et droite de la tête
    w = PlaceInFront(head, 0.5f);
    assert(near(w.position.y, 1.1f) && near(Rotate(w.orientation, {1.f, 0.f, 0.f}).x, 1.f) && near(Rotate(w.orientation, {0.f, 0.f, 1.f}).y, 1.f));
    // Sur la main, au-dessus de la manette : posée par son bord inférieur.
    const Pose base = HandPlacementPose(Hand::Right, HandPlacement::Above, 0.01f, 0.02f);
    const Pose raised = WindowHandPose(Hand::Right, HandPlacement::Above, 0.01f, 0.02f, 0.30f);
    const Vec3 shift = raised.position - base.position, upward = Rotate(base.orientation, {0.f, 1.f, 0.f});
    assert(near(Length(shift), 0.15f) && near(Dot(shift, upward), 0.15f));
    const Pose wrist = WindowHandPose(Hand::Left, HandPlacement::WristInside, 0.f, 0.f, 0.30f), wristBase = HandPlacementPose(Hand::Left, HandPlacement::WristInside);
    assert(near(wrist.position.x, wristBase.position.x) && near(wrist.position.y, wristBase.position.y) && near(wrist.position.z, wristBase.position.z));
    // Réglages reçus de XRPerfSensors : bornés.
    SettingsBlock b = {};
    b.windowEnabled = 1; b.windowAnchor = 7; b.windowPlacement = 9; b.windowWidth = 9.f; b.windowAlong = std::nanf(""); b.windowLift = -1.f; b.windowDistance = 0.01f;
    Config cfg; ApplySettings(cfg, b);
    assert(cfg.window.enabled && cfg.window.anchor == WindowAnchor::World && cfg.window.placement == HandPlacement::Above && cfg.window.width == kMaxWindowWidth);
    assert(cfg.window.along == 0.f && cfg.window.lift == kMinHandLift && cfg.window.distance == kMinWindowDistance);
    // Suivi des manettes : demandé pour une fenêtre sur une main, même masquée (Ctrl+Alt+F en cours de partie).
    Config t; t.window.enabled = true; assert(!WantsHandTracking(t)); t.window.anchor = WindowAnchor::LeftHand; assert(WantsHandTracking(t));
    t.window.enabled = false; assert(WantsHandTracking(t));
    // Réglages reçus en cours de partie : un raccourci utilisé en jeu n'est pas annulé par un bloc qui
    // n'a pas changé ce réglage-là.
    {
        SettingsBlock first = {}; first.visible = 1; first.width = 0.36f; first.globalOpacity = 1.f; first.windowEnabled = 1; first.windowAnchor = 2;
        first.windowWidth = 0.4f; first.windowDistance = 0.8f; first.theme = 0;
        Config g; ApplySettingsUpdate(g, first, nullptr); assert(g.visible && g.window.enabled && g.window.anchor == WindowAnchor::World);
        g.visible = false; g.window.enabled = false; g.width = 0.5f;   // Ctrl+Alt+O, Ctrl+Alt+F, Ctrl+Alt+Page préc. en jeu
        SettingsBlock second = first; second.theme = 2;                 // puis la couleur changée dans la fenêtre de réglages
        ApplySettingsUpdate(g, second, &first);
        assert(!g.visible && !g.window.enabled && g.width == 0.5f && g.theme == ThemeId::Contrast);
        SettingsBlock third = second; third.windowEnabled = 0; third.windowAnchor = 0; third.visible = 0;
        g.window.enabled = true; ApplySettingsUpdate(g, third, &second);   // changés dans la fenêtre : repris
        assert(!g.window.enabled && g.window.anchor == WindowAnchor::LeftHand && !g.visible && g.width == 0.5f);
    }
    // Image : recadrage, réduction, opacité.
    const CropRect r = WindowCrop(1920, 1080, 0.70f, 0.f, 0.f, 0.f);  // borné à 45 %
    assert(r.left == 864 && r.width() == 1056 && r.height() == 1080);
    int ow = 0, oh = 0; WindowOutputSize(r.width(), r.height(), kWindowMaxWidth, kWindowMaxHeight, ow, oh); assert(ow == 1056 && oh == 1080);
    WindowOutputSize(2560, 1440, kWindowMaxWidth, kWindowMaxHeight, ow, oh); assert(ow == 1280 && oh == 720);
    std::vector<uint8_t> src(4 * 2 * 2, 200), dst(4);
    DownscaleWindowImage(src.data(), 8, 2, 2, dst.data(), 1, 1, 128);
    assert(dst[0] == 100 && dst[3] == 128);
    std::printf("tests de la fenetre capturee : OK\n");
}


// Langue : le bloc de réglages choisit le français ou l'anglais ; panneau, montre et résumé suivent.
static void testLanguage(const std::string& dir) {
    Config c;
    SettingsBlock s = {};
    s.magic = kSettingsBlockMagic; s.version = kSettingsBlockVersion;
    s.width = 0.36f; s.globalOpacity = 1.f; s.distance = 1.f; s.watchSize = 0.06f; s.watchOpacity = 1.f;
    s.windowWidth = 0.4f; s.windowDistance = 0.8f;
    c.english = true;
    s.language = 0; ApplySettings(c, s); assert(c.english);        // 0 : la langue en cours ne change pas
    s.language = 1; ApplySettings(c, s); assert(!c.english);
    s.language = 2; ApplySettings(c, s); assert(c.english);
    s.language = 7; ApplySettings(c, s); assert(c.english);        // valeur inconnue : ignorée

    // Panneau : le décor est refait quand la langue change, et l'image diffère.
    PanelData d;
    FillPreviewData(d, 5.0);
    d.sensorsOk = true; d.cpuLoad = 35.f; d.gpuLoad = 73.f; d.vramUsedMB = 20000.f; d.vramTotalMB = 32000.f; d.showInfo = true;
    d.statusElements = kWatchSession | kWatchBattery; d.sessionSeconds = 100.0;
    Panel panel;
    panel.render(d, 1.f, 1.f);
    std::vector<uint8_t> french(panel.pixels(), panel.pixels() + static_cast<size_t>(Panel::kWidth) * Panel::kHeight * 4);
    d.english = true;
    panel.render(d, 1.f, 1.f);
    assert(std::memcmp(french.data(), panel.pixels(), french.size()) != 0);
    writePpm(panel, dir + "/panel_english.ppm", 96, 128, 112, Panel::kHeight);
    Panel fresh;
    fresh.render(d, 1.f, 1.f);
    assert(!std::memcmp(fresh.pixels(), panel.pixels(), french.size()));   // même image qu'un panneau neuf : décor refait
    d.english = false;
    panel.render(d, 1.f, 1.f);
    assert(!std::memcmp(french.data(), panel.pixels(), french.size()));

    // Aperçu : la langue vient du bloc.
    std::vector<uint8_t> px(french.size());
    s.headsetInfo = 1; s.layout = 0; s.language = 2;
    assert(xrpoRenderPreview(&s, nullptr, 5.0, 600, "", px.data(), static_cast<uint32_t>(px.size())) > 0);
    std::vector<uint8_t> pxFrench(px.size());
    s.language = 1;
    assert(xrpoRenderPreview(&s, nullptr, 5.0, 600, "", pxFrench.data(), static_cast<uint32_t>(pxFrench.size())) > 0);
    assert(px != pxFrench);

    // Montre : lettres des manettes G / D ou L / R.
    WatchData w; w.style = WatchStyle::Sport; w.battery[0] = 0.5f; w.battery[1] = 0.9f; w.hour = 12;
    WatchFace face;
    face.render(w, 1.f);
    std::vector<uint8_t> wf(face.pixels(), face.pixels() + static_cast<size_t>(WatchFace::kSize) * WatchFace::kSize * 4);
    w.english = true;
    face.render(w, 1.f);
    assert(std::memcmp(wf.data(), face.pixels(), wf.size()) != 0);

    // Résumé en anglais : intitulés lus par sensors/SessionReport.cs.
    SessionStats st;
    PanelData p;
    p.fpsValid = true; p.targetFps = 72.f; p.sensorsOk = true; p.cpuLoad = 30.f; p.gpuLoad = 80.f; p.vramUsedMB = 10000.f; p.vramTotalMB = 24000.f;
    double t = 100.0;
    for (int second = 0; second < 180; ++second) {
        for (int i = 0; i < 72; ++i) { t += 1.0 / 72.0; st.onFrame(t, 8.f); }
        for (int k = 0; k < 4; ++k) { p.fps = 72.f; p.gpuFrameEstimateMs = p.gpuFrameMs = 11.f; st.onSample(p, true, t); }
    }
    SessionInfo info;
    info.game = "Game.exe"; info.api = "Direct3D 11"; info.startText = "2026-10-18 09:00:00"; info.nowText = "2026-10-18 09:03:00";
    info.finished = true; info.wallSeconds = 180.0; info.english = true; info.cpu = "CPU X"; info.gpu = "";
    const std::string en = st.format(info);
    assert(has(en, "XR Perf Overlay - session summary\r\n") && has(en, "Game             : Game.exe\r\n") && has(en, "Graphics card    : --\r\n"));
    assert(has(en, "Duration         : 3 min 00 s, of which ") && has(en, "\r\nFRAMES PER SECOND\r\n") && has(en, "  Target         : 72, held 100 % of the time\r\n"));
    assert(has(en, "FRAME TIME (ms)            average       max\r\n") && has(en, "  GPU (estimated)        ") && has(en, "GRAPHICS CARD  "));
    assert(has(en, "  VRAM (GB)  ") && has(en, "FRAME TIME DISTRIBUTION   (frame budget to hold 72 : 13.9 ms)\r\n") && has(en, "  Within budget           : 100.0 %\r\n"));
    assert(has(en, "MINUTE BY MINUTE   (GPU ms : estimated)\r\n") && has(en, "FPS avg") && en.find("Jeu ") == std::string::npos);
    assert(en.find("\xC3\xA9") == std::string::npos);   // aucun mot français accentué
    writeFile(dir + "/resume_english.txt", en);
    info.english = false;
    assert(has(st.format(info), "IMAGES PAR SECONDE"));
    std::printf("tests de la langue : OK\n");
}

// Rapports : mode automatique, manuel (raccourci) ou aucun, et changement de mode en jeu.
static void testReportControl(const std::string& dir) {
    assert(NameIndex(kReportModeNames, "manual", 0) == 1 && NameIndex(kReportModeNames, "off", 0) == 2 && NameIndex(kReportModeNames, "x", -1) == -1);
    assert(sizeof(kReportModeNames) / sizeof(kReportModeNames[0]) == static_cast<size_t>(ReportMode::Count));
    // Bloc de réglages : 0 laisse le mode du .ini, 1 à 3 le choisissent, au-delà ignoré.
    Config cfg;
    SettingsBlock b = {};
    b.width = cfg.width; b.globalOpacity = cfg.globalOpacity; b.distance = cfg.distance;
    ApplySettings(cfg, b);
    assert(cfg.reportMode == ReportMode::Auto);
    b.reportMode = 2; ApplySettings(cfg, b); assert(cfg.reportMode == ReportMode::Manual);
    b.reportMode = 3; ApplySettings(cfg, b); assert(cfg.reportMode == ReportMode::Off);
    b.reportMode = 9; ApplySettings(cfg, b); assert(cfg.reportMode == ReportMode::Off);
    b.reportMode = 0; ApplySettings(cfg, b); assert(cfg.reportMode == ReportMode::Off);
    assert(offsetof(SettingsBlock, reportMode) == 188 && sizeof(SettingsBlock) == 192);

    // Automatique : enregistre dès le début, le raccourci ne fait rien.
    ReportControl rc;
    assert(rc.begin(ReportMode::Auto, false));
    ReportControl::Step st = rc.update(ReportMode::Auto, true, true);
    assert(!st.start && !st.stop);
    // Passage en manuel en cours de jeu : l'enregistrement automatique s'arrête, rien ne repart.
    st = rc.update(ReportMode::Manual, true, false);
    assert(st.stop && !st.start);
    // Appui : démarre (manuel) ; touche maintenue : rien ; relâchée puis appui : arrêt.
    st = rc.update(ReportMode::Manual, false, true);
    assert(st.start && st.manual && !st.stop);
    st = rc.update(ReportMode::Manual, true, true);
    assert(!st.start && !st.stop);
    double secs = 0.0;
    assert(rc.indicator(10.0, true, 7.0, secs) == kRecordRunning && secs == 3.0);
    st = rc.update(ReportMode::Manual, true, false);
    assert(!st.start && !st.stop);
    st = rc.update(ReportMode::Manual, true, true);
    assert(st.stop && !st.start);
    rc.stoppedManual(20.0, true, 13.0);
    assert(rc.indicator(22.0, false, 50.0, secs) == kRecordSaved && secs == 13.0);   // durée figée à l'arrêt
    assert(rc.indicator(20.0 + kRecordNoticeSeconds + 0.1, false, 0.0, secs) == kRecordNone && secs == 0.0);
    rc.stoppedManual(30.0, false, 2.0);
    assert(rc.indicator(31.0, false, 0.0, secs) == kRecordTooShort);
    // Rapport gardé : manuel, 5 s de durée avec des images (même en pause une partie du temps) ; automatique, 15 s d'images.
    assert(KeepReport(true, 1.0, 6.0) && !KeepReport(true, 0.0, 60.0) && !KeepReport(true, 4.0, 4.9));
    assert(KeepReport(false, 15.0, 15.0) && !KeepReport(false, 14.0, 600.0));
    // Retour en automatique : un enregistrement repart ; aucun : il s'arrête.
    st = rc.update(ReportMode::Auto, false, false);
    assert(st.start && !st.manual && !st.stop);
    st = rc.update(ReportMode::Off, true, false);
    assert(st.stop && !st.start);
    st = rc.update(ReportMode::Off, false, true);
    assert(!st.start && !st.stop);
    // Session commencée touche enfoncée (en manuel) : il faut la relâcher avant qu'elle compte.
    assert(!rc.begin(ReportMode::Manual, true));
    st = rc.update(ReportMode::Manual, false, true);
    assert(!st.start);

    // Nouveau rapport en cours de session : la mise à l'écart de la mesure GPU reste notée.
    {
        SessionStats r;
        for (int i = 0; i <= 90; ++i) r.onFrame(i / 90.0, 5.f);
        r.rejectGpuMeasure(0.4f, 9.f, 90.f);
        r.restart();
        assert(r.activeSeconds() == 0.0);
        SessionInfo ri; ri.finished = true;
        for (int i = 0; i <= 90; ++i) r.onFrame(2.0 + i / 90.0, 5.f);
        assert(has(r.format(ri), "Mesure réelle du temps GPU écartée"));
    }

    // Ligne d'en-tête d'un rapport manuel, dans les deux langues.
    SessionStats stats;
    for (int i = 0; i <= 900; ++i) stats.onFrame(i / 90.0, 5.f);
    SessionInfo info;
    info.manual = true; info.finished = true; info.wallSeconds = 10.0;
    assert(has(stats.format(info), "Enregistrement   : manuel"));
    info.english = true;
    assert(has(stats.format(info), "Recording        : manual"));
    info.manual = false;
    assert(stats.format(info).find("Recording") == std::string::npos);

    // Témoin sur le panneau, dans les trois présentations.
    Panel panel;
    PanelData d;
    FillPreviewData(d, 100.0);
    d.recordState = kRecordRunning; d.recordSeconds = 83.0;
    for (int layout = 0; layout < 3; ++layout) {
        d.layout = static_cast<Layout>(layout);
        panel.render(d, 0.82f);
        writePpm(panel, dir + "/panel_rec_" + std::to_string(layout) + ".ppm", 96, 128, 112, Panel::contentHeight(d));
    }
    d.layout = Layout::Compact;
    d.recordState = kRecordSaved; d.recordSeconds = 4000.0;
    panel.render(d, 0.82f);
    writePpm(panel, dir + "/panel_rec_saved.ppm", 96, 128, 112, Panel::contentHeight(d));
    d.recordState = kRecordTooShort; d.english = true;
    panel.render(d, 0.82f);
    writePpm(panel, dir + "/panel_rec_short.ppm", 96, 128, 112, Panel::contentHeight(d));
    std::printf("tests des rapports manuels : OK\n");
}

int main(int argc, char** argv) {
    testStats();
    testAsciiText();
    testInfoLine();
    testFrameHistory();
    testSettings();
    testPreviewApi();
    testLayouts();
    testRateMeter();
    testFrameTimer();
    testSessionStats(argc > 1 ? argv[1] : ".");
    testPoseMath();
    testVrMath();
    testWindow();
    testLanguage(argc > 1 ? argv[1] : ".");
    testReportControl(argc > 1 ? argv[1] : ".");
    const std::string dir = argc > 1 ? argv[1] : ".";
    Panel panel;

    PanelData d;
    d.fpsValid = true;
    d.fps = 90.f; d.fpsAvg = 88.4f; d.fpsMin = 71.f; d.fpsMax = 90.f; d.targetFps = 90.f;
    d.cpuFrameMs = 6.2f; d.gpuFrameMs = 9.4f;
    d.sensorsOk = true;
    d.cpuLoad = 34.f; d.cpuTemp = 62.f; d.cpuMHz = 5250.f;
    d.gpuLoad = 85.f; d.gpuTemp = 71.f; d.gpuMHz = 2610.f;
    d.vramUsedMB = 9.8f * 1024.f; d.vramTotalMB = 24.f * 1024.f;
    panel.render(d, 0.82f);
    writePpm(panel, dir + "/panel_ok.ppm", 96, 128, 112);

    // Avec génération d'images et horloge : 36 images du jeu, 72 envoyées au casque.
    {
        PanelData g = d;
        g.fps = 36.f; g.fpsAvg = 36.f; g.fpsMin = 36.f; g.fpsMax = 36.f; g.targetFps = 36.f; g.outFps = 72.f;
        g.cpuFrameMs = 15.6f; g.gpuFrameMs = 20.3f; g.cpuLoad = 35.f; g.cpuTemp = 57.f; g.cpuMHz = 5040.f;
        g.gpuLoad = 73.f; g.gpuTemp = 61.f; g.gpuMHz = 2857.f; g.vramUsedMB = 23.4f * 1024.f; g.vramTotalMB = 31.8f * 1024.f;
        g.clockMinutes = 17 * 60 + 36;
        panel.render(g, 0.82f);
        writePpm(panel, dir + "/panel_fg.ppm", 96, 128, 112);
        // Avec la ligne casque / résolution de rendu.
        PanelData h = g;
        h.showInfo = true; h.renderWidth = 3168; h.renderHeight = 3168;
        std::snprintf(h.headset, sizeof(h.headset), "Pimax Crystal Light");
        panel.render(h, 0.82f);
        writePpm(panel, dir + "/panel_info.ppm", 96, 128, 112, Panel::kHeight);
        std::snprintf(h.headset, sizeof(h.headset), "SteamVR/OpenXR : un nom de casque vraiment long");
        h.renderWidth = 0;
        panel.render(h, 0.82f);
        writePpm(panel, dir + "/panel_info_long.ppm", 96, 128, 112, Panel::kHeight);
        h.headset[0] = '\0'; h.sensorsOk = false; h.renderWidth = 12840; h.renderHeight = 10240;
        panel.render(h, 0.82f);
        writePpm(panel, dir + "/panel_info_unknown.ppm", 96, 128, 112, Panel::kHeight);
        // Ligne session / batterie (option), au-dessus de la ligne casque.
        h.statusElements = kWatchSession | kWatchBattery; h.sessionSeconds = 1 * 3600 + 23 * 60 + 45; h.battery[0] = 0.8f; h.battery[1] = 0.35f;
        h.sensorsOk = true; std::snprintf(h.headset, sizeof(h.headset), "Pimax Dream Air"); h.renderWidth = 5190; h.renderHeight = 4168;
        panel.render(h, 0.82f);
        writePpm(panel, dir + "/panel_status.ppm", 96, 128, 112, Panel::kHeight);

        // Présentations et couleurs : une image par combinaison utile pour la documentation.
        PanelData v;
        FillPreviewData(v, 1234.5);
        v.fps = 36.f; v.fpsAvg = 36.f; v.fpsMin = 31.f; v.fpsMax = 36.f; v.targetFps = 36.f; v.outFps = 72.f;
        for (int i = 0; i < PanelData::kGraphColumns; ++i) { v.graphFrameMs[i] *= 2.5f; v.graphCpuMs[i] *= 2.5f; v.graphGpuMs[i] *= 2.5f; }
        v.cpuFrameMs = 15.6f; v.gpuFrameMs = 20.3f; v.sensorsOk = true; v.cpuLoad = 35.f; v.cpuTemp = 57.f; v.cpuMHz = 5040.f;
        v.gpuLoad = 73.f; v.gpuTemp = 61.f; v.gpuMHz = 2857.f; v.vramUsedMB = 23.4f * 1024.f; v.vramTotalMB = 31.8f * 1024.f;
        v.clockMinutes = 17 * 60 + 36; v.showInfo = true; v.renderWidth = 3168; v.renderHeight = 3168;
        std::snprintf(v.headset, sizeof(v.headset), "Pimax Crystal Light");
        const char* layoutName[] = {"classic", "graph", "compact"};
        for (int layout = 0; layout < 3; ++layout) {
            for (int theme = 0; theme < static_cast<int>(ThemeId::Count); ++theme) {
                v.layout = static_cast<Layout>(layout);
                v.theme = theme == static_cast<int>(ThemeId::Custom) ? CustomTheme({24, 28, 40}, {255, 255, 255}, {255, 110, 160}) : PresetTheme(static_cast<ThemeId>(theme));
                panel.render(v, 0.82f);
                writePpm(panel, dir + "/look_" + layoutName[layout] + "_" + std::to_string(theme) + ".ppm", 96, 128, 112, Panel::contentHeight(v.layout, true, false));
            }
        }
        v.layout = Layout::Graph; v.theme = PresetTheme(ThemeId::Dark); v.sensorsOk = false; v.showInfo = false;
        panel.render(v, 0.82f);
        writePpm(panel, dir + "/look_graph_nosensors.ppm", 96, 128, 112, Panel::kBodyHeight);
        // Sans capteurs mais avec la mesure réelle du temps GPU : la ligne GPU garde ce temps.
        v.gpuMeasured = true; v.gpuFrameMs = 9.4f;
        panel.render(v, 0.82f);
        writePpm(panel, dir + "/look_graph_nosensors_measured.ppm", 96, 128, 112, Panel::kBodyHeight);
        v.gpuMeasured = false;
        // Pire cas de largeur : trois chiffres partout.
        g.fps = 120.f; g.fpsAvg = 119.8f; g.fpsMin = 101.f; g.fpsMax = 120.f; g.targetFps = 120.f; g.outFps = 240.f; g.sensorsOk = false;
        panel.render(g, 0.82f);
        writePpm(panel, dir + "/panel_fg_wide.ppm", 96, 128, 112);
    }
    // Même panneau à 50 % d'opacité d'ensemble (réglage global_opacity).
    panel.render(d, 0.82f, 0.5f);
    writePpm(panel, dir + "/panel_half.ppm", 96, 128, 112);
    panel.render(d, 0.82f);

    // Cas dégradé : sous la cadence, températures hautes, VRAM saturée.
    d.fps = 52.f; d.fpsAvg = 61.7f; d.fpsMin = 38.f; d.fpsMax = 120.f; d.targetFps = 120.f;
    d.cpuFrameMs = 7.9f; d.gpuFrameMs = 18.6f;
    d.cpuLoad = 100.f; d.cpuTemp = 91.f; d.gpuLoad = 99.f; d.gpuTemp = 88.f; d.gpuMHz = 2850.f;
    d.vramUsedMB = 15.7f * 1024.f; d.vramTotalMB = 16.f * 1024.f;
    panel.render(d, 0.82f);
    writePpm(panel, dir + "/panel_stress.ppm", 200, 205, 210);

    // Montre : trois styles, valeurs complètes puis partielles.
    {
        WatchFace watch;
        WatchData w;
        w.hour = 21; w.minute = 47; w.second = 23; w.sessionSeconds = 1 * 3600 + 23 * 60 + 45;
        w.battery[0] = 0.82f; w.battery[1] = 0.17f; w.charging[1] = false;
        w.fpsValid = true; w.fps = 71.f; w.targetFps = 72.f;
        const char* names[] = {"classic", "digital", "sport"};
        for (int style = 0; style < static_cast<int>(WatchStyle::Count); ++style) {
            w.style = static_cast<WatchStyle>(style);
            watch.render(w);
            // Coins transparents, centre opaque.
            assert(watch.pixels()[3] == 0 || w.style == WatchStyle::Digital);
            const size_t center = (static_cast<size_t>(WatchFace::kSize / 2) * WatchFace::kSize + WatchFace::kSize / 2) * 4;
            assert(watch.pixels()[center + 3] == 255);
            writeRgbaPpm(watch.pixels(), WatchFace::kSize, WatchFace::kSize, dir + "/watch_" + names[style] + ".ppm", 40, 44, 52);
            // Sans batterie connue, sans session ni FPS : rien ne casse.
            WatchData bare = w; bare.battery[0] = bare.battery[1] = std::nanf(""); bare.elements = 0; bare.hour = -1;
            watch.render(bare, 0.6f);
            writeRgbaPpm(watch.pixels(), WatchFace::kSize, WatchFace::kSize, dir + "/watch_" + names[style] + "_bare.ppm", 40, 44, 52);
        }
        // Thème clair, une seule manette connue, en charge.
        w.style = WatchStyle::Digital; w.theme = PresetTheme(ThemeId::Light); w.battery[0] = std::nanf(""); w.battery[1] = 0.55f; w.charging[1] = true;
        watch.render(w);
        writeRgbaPpm(watch.pixels(), WatchFace::kSize, WatchFace::kSize, dir + "/watch_digital_light.ppm", 200, 205, 210);
        std::printf("tests de la montre : OK\n");
    }

    // Programme de capteurs non lancé, cadence pas encore mesurée.
    PanelData e;
    e.targetFps = 90.f;
    panel.render(e, 0.82f);
    writePpm(panel, dir + "/panel_nosensors.ppm", 30, 40, 60);
    return 0;
}
