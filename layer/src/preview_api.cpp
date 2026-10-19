#include "preview_api.h"

#include <cmath>
#include <cstring>

#include "ascii.h"
#include "config.h"
#include "frame_history.h"
#include "settings_apply.h"

namespace xrpo {

namespace {

// Nombre pseudo-aléatoire stable entre 0 et 1 pour un indice donné.
float noise(int64_t index) {
    uint64_t x = static_cast<uint64_t>(index) * 0x9E3779B97F4A7C15ull;
    x ^= x >> 29;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 32;
    return static_cast<float>(x & 0xFFFF) / 65535.f;
}

}  // namespace

void FillPreviewData(PanelData& data, double seconds) {
    // Un jeu qui tient 90 images par seconde, avec un à-coup de temps en temps.
    const float target = 90.f, budget = 1000.f / target;
    const int64_t newest = static_cast<int64_t>(std::floor(seconds / FrameHistory::kSliceSeconds));
    float worst = 0.f;
    for (int i = 0; i < PanelData::kGraphColumns; ++i) {
        const int64_t slice = newest - (PanelData::kGraphColumns - 1 - i);
        float frame = budget + (noise(slice) - 0.5f) * 0.5f;
        const int64_t phase = ((slice % 137) + 137) % 137;  // un à-coup toutes les 4,5 secondes environ
        if (phase < 3) frame = budget * (phase == 1 ? 1.9f : 1.45f);
        data.graphFrameMs[i] = frame;
        data.graphCpuMs[i] = 5.6f + noise(slice + 7919) * 1.4f + (phase < 3 ? 3.f : 0.f);
        // GPU estimé : relevé quatre fois par seconde, donc par paliers de sept à huit tranches.
        const int64_t step = slice / 8;
        data.graphGpuMs[i] = 7.9f + noise(step + 104729) * 0.8f + (phase < 3 ? 1.5f : 0.f);
        if (frame > worst) worst = frame;
    }
    data.graphValid = true;

    const bool hitch = data.graphFrameMs[PanelData::kGraphColumns - 1] > budget * 1.2f;
    data.fpsValid = true;
    data.targetFps = target;
    data.fps = hitch ? 84.f : 90.f;
    data.fpsAvg = 89.2f;
    data.fpsMin = worst > budget * 1.2f ? 84.f : 90.f;
    data.fpsMax = 90.f;
    data.cpuFrameMs = data.graphCpuMs[PanelData::kGraphColumns - 1];
    data.gpuMeasured = false;
}

}  // namespace xrpo

int xrpoRenderPreview(const xrpo::SettingsBlock* settings, const xrpo::SensorBlock* sensors, double seconds, int clockMinutes,
                      const char* headsetName, uint8_t* pixels, uint32_t capacity) {
    using namespace xrpo;
    const size_t bytes = static_cast<size_t>(Panel::kWidth) * Panel::kHeight * 4;
    if (!settings || !pixels || capacity < bytes || !(seconds >= 0.0) || seconds > 1e9) return 0;
    // Le bloc est lu en entier : il doit venir d'un XRPerfSensors de la même version (sinon, sa taille diffère).
    if (settings->magic != kSettingsBlockMagic || settings->version != kSettingsBlockVersion) return 0;
    try {
        Config config;
        ApplySettings(config, *settings);

        PanelData data;
        FillPreviewData(data, seconds);
        if (sensors && sensors->magic == kSharedBlockMagic && sensors->version == kSharedBlockVersion) {
            data.sensorsOk = true;
            data.cpuLoad = sensors->cpuLoad;
            data.cpuTemp = sensors->cpuTemp;
            data.cpuMHz = sensors->cpuMHz;
            data.gpuLoad = sensors->gpuLoad;
            data.gpuTemp = sensors->gpuTemp;
            data.gpuMHz = sensors->gpuMHz;
            data.vramUsedMB = sensors->vramUsedMB;
            data.vramTotalMB = sensors->vramTotalMB;
            if (!std::isnan(sensors->gpuLoad)) data.gpuFrameMs = data.gpuFrameEstimateMs = (sensors->gpuLoad / 100.f) * (1000.f / data.fps);
        }
        if (config.clock && clockMinutes >= 0) data.clockMinutes = clockMinutes;
        data.showInfo = config.headsetInfo;
        // Ligne session / batterie : valeurs d'exemple, comme sur l'aperçu de la montre.
        data.statusElements = config.panelElements;
        data.sessionSeconds = 1 * 3600 + 23 * 60 + 45;
        data.battery[0] = 0.8f;
        data.battery[1] = 0.35f;
        const std::string name = headsetName ? AsciiText(headsetName, 256) : std::string();
        std::memcpy(data.headset, name.c_str(), name.size() + 1);  // AsciiText borne la longueur à 47 caractères
        data.layout = config.layout;
        data.theme = ThemeOf(config);
        data.english = config.english;

        // Comme pour un jeu : la fenêtre du bureau montre toujours le panneau opaque.
        Panel panel;
        panel.render(data, 1.f, 1.f);
        std::memcpy(pixels, panel.pixels(), bytes);
        return Panel::contentHeight(data);
    } catch (...) {
        return 0;  // mémoire insuffisante : pas d'aperçu
    }
}

int xrpoRenderWatchPreview(const xrpo::SettingsBlock* settings, int clockSeconds, uint8_t* pixels, uint32_t capacity) {
    using namespace xrpo;
    const size_t bytes = static_cast<size_t>(WatchFace::kSize) * WatchFace::kSize * 4;
    if (!settings || !pixels || capacity < bytes || clockSeconds < 0 || clockSeconds >= 24 * 3600) return 0;
    if (settings->magic != kSettingsBlockMagic || settings->version != kSettingsBlockVersion) return 0;
    try {
        Config config;
        ApplySettings(config, *settings);
        WatchData data;
        data.style = config.watch.style;
        data.elements = config.watch.elements;
        data.theme = ThemeOf(config);
        data.english = config.english;
        data.hour = clockSeconds / 3600;
        data.minute = (clockSeconds / 60) % 60;
        data.second = clockSeconds % 60;
        data.sessionSeconds = 1 * 3600 + 23 * 60 + 45;
        data.battery[0] = 0.8f;
        data.battery[1] = 0.35f;
        data.fpsValid = true;
        data.fps = 89.f;
        data.targetFps = 90.f;
        WatchFace watch;
        watch.render(data, config.watch.opacity);
        std::memcpy(pixels, watch.pixels(), bytes);
        return WatchFace::kSize;
    } catch (...) {
        return 0;
    }
}
