// Ce que la layer OpenXR et XRPerfVR font de la même façon pour l'écran tactile : état affiché par les
// tuiles, et raccourci clavier équivalent à une tuile.
#pragma once

#include <cmath>
#include <cstdio>
#include <cstring>

#include "command_share.h"
#include "config.h"
#include "settings_apply.h"
#include "touch_pad.h"

namespace xrpo {

// Raccourci (ordre de Hotkeys::poll) qui fait la même chose qu'une tuile ; -1 : aucun.
inline int TouchHotkeyIndex(TouchAction a) {
    switch (a) {
        case TouchAction::Panel: return 0;
        case TouchAction::PanelPlace: return 1;
        case TouchAction::ResetStats: return 2;
        case TouchAction::Window: return 7;
        case TouchAction::WindowPlace: return 8;
        case TouchAction::Filters: return 9;
        default: return -1;
    }
}

// Ce que seul celui qui affiche l'écran connaît.
struct TouchInputs {
    double videoSeconds = -1.0;  // enregistrement ou direct en cours (< 0 : aucun)
    bool live = false;
    bool reportAvailable = true;  // XRPerfVR n'écrit pas le rapport d'un jeu mesuré par la layer
    bool reportRunning = false;
    double reportSeconds = 0.0;
};

inline TouchState MakeTouchState(const Config& c, const CommandShare::State& remote, const TouchInputs& in, int clockSeconds) {
    TouchState s;
    s.theme = ThemeOf(c);
    s.english = c.english;
    if (clockSeconds >= 0) {
        s.hour = clockSeconds / 3600;
        s.minute = (clockSeconds / 60) % 60;
    }
    s.sensors = remote.online;
    s.videoReady = remote.online && (remote.flags & kCommandVideoReady) != 0;
    s.liveReady = remote.online && (remote.flags & kCommandLiveReady) != 0;
    s.videoSeconds = in.videoSeconds;
    s.live = in.live;
    s.reportManual = in.reportAvailable && c.reportMode == ReportMode::Manual;
    s.reportRunning = s.reportManual && in.reportRunning;
    s.reportSeconds = in.reportSeconds;
    s.filtersAllowed = c.imageAllowed && c.image.mode != 0;
    s.filtersOn = s.filtersAllowed && !c.imageBypass;
    std::snprintf(s.preset, sizeof(s.preset), "%s", remote.online ? remote.preset : "");
    s.panelVisible = c.visible;
    s.panelOnView = c.anchor == Anchor::View;
    s.windowReady = remote.online && (remote.flags & kCommandWindowReady) != 0;
    s.windowVisible = c.window.enabled;
    return s;
}

// Même image : les durées comptent à la seconde.
inline bool SameTouchState(const TouchState& a, const TouchState& b) {
    const auto secs = [](double v) { return v < 0.0 ? -1L : static_cast<long>(std::floor(v)); };
    return std::memcmp(&a.theme, &b.theme, sizeof(Theme)) == 0 && a.english == b.english && a.hour == b.hour && a.minute == b.minute &&
           a.sensors == b.sensors && a.videoReady == b.videoReady && a.liveReady == b.liveReady && secs(a.videoSeconds) == secs(b.videoSeconds) &&
           a.live == b.live && a.reportManual == b.reportManual && a.reportRunning == b.reportRunning &&
           (!a.reportRunning || secs(a.reportSeconds) == secs(b.reportSeconds)) && a.filtersAllowed == b.filtersAllowed &&
           a.filtersOn == b.filtersOn && std::strcmp(a.preset, b.preset) == 0 && a.panelVisible == b.panelVisible &&
           a.panelOnView == b.panelOnView && a.windowReady == b.windowReady && a.windowVisible == b.windowVisible;
}

}  // namespace xrpo
