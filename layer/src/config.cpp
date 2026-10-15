#include "config.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <cwctype>

#include "ascii.h"
#include "settings_apply.h"

namespace xrpo {

namespace {

const wchar_t* const kCornerNames[] = {L"top-left", L"top", L"top-right", L"bottom-right", L"bottom", L"bottom-left"};

std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return s;
}

std::wstring trim(const std::wstring& s) {
    const size_t a = s.find_first_not_of(L" \t\"");
    if (a == std::wstring::npos) return L"";
    const size_t b = s.find_last_not_of(L" \t\"");
    return s.substr(a, b - a + 1);
}

std::wstring readString(const Config& c, const wchar_t* section, const wchar_t* key) {
    wchar_t buf[1024];
    GetPrivateProfileStringW(section, key, L"", buf, 1024, c.iniPath.c_str());
    std::wstring value = buf;
    const size_t comment = value.find(L';');  // commentaire en fin de ligne
    if (comment != std::wstring::npos) value.erase(comment);
    return trim(value);
}

float readFloat(const Config& c, const wchar_t* section, const wchar_t* key, float fallback, float lo, float hi) {
    std::wstring s = readString(c, section, key);
    if (s.empty()) return fallback;
    std::replace(s.begin(), s.end(), L',', L'.');  // "0,34" accepté
    wchar_t* end = nullptr;
    const double v = std::wcstod(s.c_str(), &end);
    if (end == s.c_str()) return fallback;
    return static_cast<float>(std::min<double>(hi, std::max<double>(lo, v)));
}

int readInt(const Config& c, const wchar_t* section, const wchar_t* key, int fallback, int lo, int hi) {
    const std::wstring s = readString(c, section, key);
    if (s.empty()) return fallback;
    wchar_t* end = nullptr;
    const long v = std::wcstol(s.c_str(), &end, 0);  // base 0 : accepte 79 comme 0x4F
    if (end == s.c_str()) return fallback;
    return static_cast<int>(std::min<long>(hi, std::max<long>(lo, v)));
}

// Deux décimales, toujours avec un point, quels que soient les réglages régionaux.
std::wstring formatFloat(float value) {
    const int hundredths = static_cast<int>(value * 100.f + 0.5f);
    wchar_t buf[32];
    swprintf(buf, 32, L"%d.%02d", hundredths / 100, hundredths % 100);
    return buf;
}

// Quatre décimales, avec un point, signe compris (pose de la fenêtre dans l'espace).
std::wstring formatPrecise(float value) {
    const bool negative = value < 0.f;
    const long long tenThousandths = static_cast<long long>((negative ? -value : value) * 10000.f + 0.5f);
    wchar_t buf[48];
    swprintf(buf, 48, L"%ls%lld.%04lld", negative && tenThousandths != 0 ? L"-" : L"", tenThousandths / 10000, tenThousandths % 10000);
    return buf;
}

std::wstring formatPose(const Pose& p) {
    const float values[7] = {p.position.x, p.position.y, p.position.z, p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w};
    std::wstring text;
    for (int i = 0; i < 7; ++i) {
        if (i) text += L' ';
        text += formatPrecise(values[i]);
    }
    return text;
}

}  // namespace

std::string Narrow(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return std::string();
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0], size, nullptr, nullptr);
    return out;
}

std::wstring CurrentExeName(bool lowercase) {
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path, n);
    const size_t slash = s.find_last_of(L"\\/");
    if (slash != std::wstring::npos) s.erase(0, slash + 1);
    return lowercase ? lower(s) : s;
}

void PinModule(void* moduleHandle) {
    if (!moduleHandle) return;
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(static_cast<HMODULE>(moduleHandle), path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, path, &pinned);
}

std::wstring FallbackSummaryFolder() {
    wchar_t base[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::wstring();
    const std::wstring root = std::wstring(base) + L"\\XRPerfOverlay";
    CreateDirectoryW(root.c_str(), nullptr);
    return root + L"\\sessions";
}

Config LoadConfig(void* moduleHandle) {
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(static_cast<HMODULE>(moduleHandle), path, MAX_PATH);
    std::wstring dir(path, n);
    const size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dir.erase(slash + 1);
    return LoadConfigFromFolder(dir);
}

Config LoadConfigFromFolder(const std::wstring& folder) {
    Config c;
    std::wstring dir = folder;
    if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/') dir += L'\\';
    c.iniPath = dir + L"XRPerfOverlay.ini";

    c.visible = readInt(c, L"overlay", L"visible", 1, 0, 1) != 0;
    const std::wstring pos = lower(readString(c, L"overlay", L"position"));
    for (int i = 0; i < static_cast<int>(Corner::Count); ++i)
        if (pos == kCornerNames[i]) c.position = static_cast<Corner>(i);
    c.distance = readFloat(c, L"overlay", L"distance", c.distance, 0.3f, 5.f);
    c.offsetX = readFloat(c, L"overlay", L"offset_x", c.offsetX, 0.f, 3.f);
    c.offsetY = readFloat(c, L"overlay", L"offset_y", c.offsetY, 0.f, 3.f);
    c.width = readFloat(c, L"overlay", L"width", c.width, kMinWidth, kMaxWidth);
    c.opacity = readFloat(c, L"overlay", L"opacity", c.opacity, 0.f, 1.f);
    c.globalOpacity = readFloat(c, L"overlay", L"global_opacity", c.globalOpacity, kMinGlobalOpacity, kMaxGlobalOpacity);
    c.faceViewer = readInt(c, L"overlay", L"face_viewer", 1, 0, 1) != 0;
    c.refreshMs = readInt(c, L"overlay", L"refresh_ms", c.refreshMs, 50, 5000);
    c.statsWindowS = readInt(c, L"overlay", L"stats_window_s", c.statsWindowS, 2, 600);

    c.keyToggle = readInt(c, L"hotkeys", L"toggle", c.keyToggle, 0, 255);
    c.keyPosition = readInt(c, L"hotkeys", L"position", c.keyPosition, 0, 255);
    c.keyReset = readInt(c, L"hotkeys", L"reset", c.keyReset, 0, 255);
    c.keySizeUp = readInt(c, L"hotkeys", L"size_up", c.keySizeUp, 0, 255);
    c.keySizeDown = readInt(c, L"hotkeys", L"size_down", c.keySizeDown, 0, 255);
    c.keyOpacityUp = readInt(c, L"hotkeys", L"opacity_up", c.keyOpacityUp, 0, 255);
    c.keyOpacityDown = readInt(c, L"hotkeys", L"opacity_down", c.keyOpacityDown, 0, 255);
    c.keyWindowToggle = readInt(c, L"hotkeys", L"window_toggle", c.keyWindowToggle, 0, 255);
    c.keyWindowPlace = readInt(c, L"hotkeys", L"window_place", c.keyWindowPlace, 0, 255);

    c.clock = readInt(c, L"overlay", L"clock", 1, 0, 1) != 0;
    c.headsetInfo = readInt(c, L"overlay", L"headset_info", 1, 0, 1) != 0;
    c.panelElements = (readInt(c, L"overlay", L"session", 0, 0, 1) ? kWatchSession : 0u) | (readInt(c, L"overlay", L"battery", 0, 0, 1) ? kWatchBattery : 0u);
    const std::wstring headset = readString(c, L"overlay", L"headset_name");
    c.headsetName = AsciiText(headset.c_str(), headset.size());

    // Présentation et couleurs : un nom inconnu laisse la valeur par défaut.
    const auto ascii = [&](const wchar_t* key) {
        const std::wstring text = lower(readString(c, L"overlay", key));
        return AsciiText(text.c_str(), text.size());
    };
    c.layout = static_cast<Layout>(NameIndex(kLayoutNames, ascii(L"layout").c_str(), static_cast<int>(c.layout)));
    c.theme = static_cast<ThemeId>(NameIndex(kThemeNames, ascii(L"theme").c_str(), static_cast<int>(c.theme)));
    ParseColor(ascii(L"custom_background").c_str(), c.customBackground);
    ParseColor(ascii(L"custom_text").c_str(), c.customText);
    ParseColor(ascii(L"custom_accent").c_str(), c.customAccent);
    // Accroche du panneau à une main.
    c.anchor = static_cast<Anchor>(NameIndex(kAnchorNames, ascii(L"anchor").c_str(), static_cast<int>(c.anchor)));
    c.handPlacement = static_cast<HandPlacement>(NameIndex(kPlacementNames, ascii(L"hand_position").c_str(), static_cast<int>(c.handPlacement)));
    c.gazeOnly = readInt(c, L"overlay", L"gaze_only", 0, 0, 1) != 0;
    c.handAlong = readFloat(c, L"overlay", L"hand_along", c.handAlong, kMinHandAlong, kMaxHandAlong);
    c.handLift = readFloat(c, L"overlay", L"hand_lift", c.handLift, kMinHandLift, kMaxHandLift);

    // Montre.
    const auto watchText = [&](const wchar_t* key) {
        const std::wstring text = lower(readString(c, L"watch", key));
        return AsciiText(text.c_str(), text.size());
    };
    WatchConfig& w = c.watch;
    w.enabled = readInt(c, L"watch", L"enabled", 0, 0, 1) != 0;
    w.anchor = static_cast<Anchor>(NameIndex(kAnchorNames, watchText(L"anchor").c_str(), static_cast<int>(w.anchor)));
    w.placement = static_cast<HandPlacement>(NameIndex(kPlacementNames, watchText(L"hand_position").c_str(), static_cast<int>(w.placement)));
    w.style = static_cast<WatchStyle>(NameIndex(kWatchStyleNames, watchText(L"style").c_str(), static_cast<int>(w.style)));
    w.elements = (readInt(c, L"watch", L"session", 1, 0, 1) ? kWatchSession : 0u) | (readInt(c, L"watch", L"battery", 1, 0, 1) ? kWatchBattery : 0u) |
                 (readInt(c, L"watch", L"fps", 1, 0, 1) ? kWatchFps : 0u);
    w.gazeOnly = readInt(c, L"watch", L"gaze_only", 1, 0, 1) != 0;
    w.size = readFloat(c, L"watch", L"size", w.size, kMinWatchSize, kMaxWatchSize);
    w.opacity = readFloat(c, L"watch", L"opacity", w.opacity, kMinWatchOpacity, kMaxWatchOpacity);
    w.along = readFloat(c, L"watch", L"hand_along", w.along, kMinHandAlong, kMaxHandAlong);
    w.lift = readFloat(c, L"watch", L"hand_lift", w.lift, kMinHandLift, kMaxHandLift);

    // Fenêtre capturée.
    const auto windowText = [&](const wchar_t* key) {
        const std::wstring text = lower(readString(c, L"window", key));
        return AsciiText(text.c_str(), text.size());
    };
    WindowConfig& win = c.window;
    win.enabled = readInt(c, L"window", L"enabled", 0, 0, 1) != 0;
    win.anchor = static_cast<WindowAnchor>(NameIndex(kWindowAnchorNames, windowText(L"anchor").c_str(), static_cast<int>(win.anchor)));
    win.placement = static_cast<HandPlacement>(NameIndex(kPlacementNames, windowText(L"hand_position").c_str(), static_cast<int>(win.placement)));
    win.gazeOnly = readInt(c, L"window", L"gaze_only", 0, 0, 1) != 0;
    win.width = readFloat(c, L"window", L"width", win.width, kMinWindowWidth, kMaxWindowWidth);
    win.along = readFloat(c, L"window", L"hand_along", win.along, kMinHandAlong, kMaxHandAlong);
    win.lift = readFloat(c, L"window", L"hand_lift", win.lift, kMinHandLift, kMaxHandLift);
    win.distance = readFloat(c, L"window", L"distance", win.distance, kMinWindowDistance, kMaxWindowDistance);
    win.worldSet = ParseWorldPose(windowText(L"world").c_str(), win.world);

    const std::wstring measure = lower(readString(c, L"framegen", L"measure_output"));
    c.measureOutput = measure == L"1" ? 1 : (measure == L"0" ? 0 : -1);
    c.summaryEnabled = readInt(c, L"summary", L"enabled", 1, 0, 1) != 0;
    c.summaryHardware = readInt(c, L"summary", L"hardware", 1, 0, 1) != 0;
    c.gpuTiming = readInt(c, L"gpu", L"measure", 0, 0, 1) != 0;
    c.summaryFolder = readString(c, L"summary", L"folder");
    if (c.summaryFolder.empty()) c.summaryFolder = dir + L"sessions";

    // Liste d'exécutables séparés par des virgules.
    std::wstring list = lower(readString(c, L"process", L"exclude"));
    size_t start = 0;
    while (start <= list.size()) {
        size_t comma = list.find(L',', start);
        if (comma == std::wstring::npos) comma = list.size();
        const std::wstring item = trim(list.substr(start, comma - start));
        if (!item.empty()) c.exclude.push_back(item);
        start = comma + 1;
    }
    return c;
}

void SaveState(const Config& c) {
    if (c.iniPath.empty()) return;
    WritePrivateProfileStringW(L"overlay", L"visible", c.visible ? L"1" : L"0", c.iniPath.c_str());
    WritePrivateProfileStringW(L"overlay", L"position", kCornerNames[static_cast<int>(c.position)], c.iniPath.c_str());
    WritePrivateProfileStringW(L"overlay", L"width", formatFloat(c.width).c_str(), c.iniPath.c_str());
    WritePrivateProfileStringW(L"overlay", L"global_opacity", formatFloat(c.globalOpacity).c_str(), c.iniPath.c_str());
    // Fenêtre capturée : affichée ou non (Ctrl+Alt+F), posée dans l'espace (Ctrl+Alt+G).
    WritePrivateProfileStringW(L"window", L"enabled", c.window.enabled ? L"1" : L"0", c.iniPath.c_str());
    const char* anchor = kWindowAnchorNames[static_cast<int>(c.window.anchor)];
    WritePrivateProfileStringW(L"window", L"anchor", std::wstring(anchor, anchor + std::strlen(anchor)).c_str(), c.iniPath.c_str());
    if (c.window.worldSet) WritePrivateProfileStringW(L"window", L"world", formatPose(c.window.world).c_str(), c.iniPath.c_str());
}

}  // namespace xrpo
