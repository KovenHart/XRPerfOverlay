# Changelog

*Français : [CHANGELOG.md](CHANGELOG.md)*

What's new in each version of XR Perf Overlay, most recent first.

## 1.7.0 (2026-10-21)

- **Configuration page** in XRPerfSensors: a diagnosis of the PC for VR. It lists the active OpenXR runtime and the other installed ones, the registered OpenXR layers (in loading order, with their state), the runtime for SteamVR games, installed VR software, the processor, memory, graphics cards and their drivers, and the Windows settings that matter (GPU scheduling, Game Mode, power mode). It points out what deserves a look: runtime missing or changed since the last session, orphaned layer or layer without its DLL, XR Perf Overlay registered twice, OpenXR Toolkit still active, two frame generation layers at once, Power saver mode, laptop with two graphics cards... A layer that causes trouble can be **turned off in one click**, with confirmation, without uninstalling it, and turned back on the same way. Nothing else is changed. "Copy diagnosis" button to ask for help (without the Windows account name: paths inside the user folder are written `%USERPROFILE%`).
- **Advice with numbers in reports**: when the graphics card is the bottleneck, the report gives the render resolution to aim for to hold the pace (for example "about 85 % of the current one, that is 3264 x 3019 per eye"), computed from the GPU time and the frame budget; when it has headroom, how far up to go. When the processor is the bottleneck, how many frames per second it can keep up with, and whether frame generation would bring the target within reach. These are estimates, to check with a new report. In a SteamVR game, the calculation starts from the resolution SteamVR recommends (the game's own cannot be seen from outside).
- **Headset name** editable in XRPerfSensors (Appearance page): useful under SteamVR, where the headset driver may report another model (a Dream Air seen as "Pimax Crystal"). It applies from the next game launch.
- Reports: when XRPerfSensors read the sensors during only a small part of the session (started mid-game or at the end), their averages are no longer used by the analysis, which could only draw wrong conclusions from them (a graphics card "barely loaded" according to a single reading); the report points it out and advises starting XRPerfSensors before the game.
- SteamVR game reports: the header shows the resolution recommended by SteamVR instead of the "not recorded" graphics API, and comparing two sessions points out when it changed.

## 1.6.0 (2026-10-19)

- **Turn the OpenXR layer off** without uninstalling it: "OpenXR games (layer)" switch in XRPerfSensors (Panel page, dashboard) and "OpenXR layer enabled" item in the tray icon menu. When off, the layer is no longer loaded into OpenXR games at all; a banner on the dashboard reminds you. An update keeps it off.
- **Reports on hotkey**: Measurements page, "Write a report" every session, on hotkey or never. On hotkey, `Ctrl+Alt+S` starts recording (red dot and "REC" with the duration at the top of the panel), then stops it and writes the report for that stretch only. The mode changes immediately, even in game. Works in SteamVR games too. In `XRPerfOverlay.ini`: `mode=` in `[summary]`, key `record=` in `[hotkeys]`.
- Reports made on hotkey are marked "manual" on the Reports page.

## 1.5.0

Released together with 1.6.0.

- **New XRPerfSensors window**: a single window with a menu on the left (dashboard, panel, appearance, watch, window, desktop mirror, measurements, reports, interface). Full screen with `F11`. Colors of your choice: like the panel, six themes or three colors of your own; text size, animations. It uses Microsoft's WebView2 engine.
- **Dashboard**: the panel as it appears in the headset, quick switches, the processor, graphics card and video memory live, the last session.
- **Reports**: analysis and minute-by-minute curves on the same page. The simple view is illustrated (grid of 100 frames, bars for what held it back, temperature and video memory gauges). Comparing two sessions shows the differences as bars, both frame grids and overlaid curves.
- **English**: everything is translated (window, tray icon menu, panel and watch in the headset, reports, installer, documentation). By default, the Windows language; change it on the Interface page.
- The old settings and reports windows are replaced by the new window.

## 1.4.3 (2026-10-15)

- Desktop window in the headset: fix when cropping changes the size of the captured image.

## 1.4.2 (2026-10-15)

- Desktop window: clearer crop setting and more frequent preview refresh.

## 1.4.1 (2026-10-15)

- SteamVR games: XRPerfVR reconnects to SteamVR restarted right after closing (end of room setup, for example).

## 1.4.0 (2026-10-15)

- **Desktop window in the headset**: a window open on the PC (stream chat, Discord...) shown read-only, placed in the room or on a hand. `Ctrl+Alt+F` shows or hides it, `Ctrl+Alt+G` places it in front of you. Crop, size, opacity and frames per second are adjustable. In OpenXR and SteamVR games.

## 1.3.2 (2026-10-14)

- SteamVR games: the panel no longer flickers or freezes (image sent to SteamVR as a Direct3D 11 texture).

## 1.3.1 (2026-10-14)

- Optional "session / battery" line at the bottom of the panel: session time and controllers' battery.

## 1.3.0 (2026-10-14)

- **SteamVR (OpenVR) games**: the panel, the watch and reports also work in games that don't go through OpenXR, thanks to XRPerfVR, started by XRPerfSensors. GPU time is measured by SteamVR there.

## 1.2.3 (2026-10-13)

- Fix: a game whose first OpenXR request was refused (for a reason unrelated to the layer) got a misleading error.

## 1.2.2 (2026-10-13)

- Headset / resolution line: the difference with the resolution recommended by the runtime is shown (for example `x1.21`), and recorded in reports.

## 1.2.1 (2026-10-13)

- Report analysis: points out a resolution changed by the game mid-session, and different settings (resolution, headset, runtime, graphics API) between two compared sessions.

## 1.2.0 (2026-10-13)

- **Watch** on the wrist or in front of the eyes: time, session time, controllers' battery, FPS, in three styles. On the wrist, it only appears when you look at it (option).
- The panel can be attached to a hand.

## 1.1.0 (2026-10-12)

- Processor and graphics card models in reports (option).
- Reports window: "Simple" view in sentences, "Detailed" view with findings, comparison of two sessions.

## 1.0.0 (2026-10-12)

- First version: performance panel in the headset for OpenXR games (FPS, CPU, GPU, VRAM), choice of layouts and colors, hotkeys, estimated or measured GPU time, desktop panel mirror for OBS, a report for each session, settings window, installer and ready-to-install package.
