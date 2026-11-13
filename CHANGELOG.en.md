# Changelog

*Français : [CHANGELOG.md](CHANGELOG.md)*

What's new in each version of XR Perf Overlay, most recent first.

## 1.13.1

- **Smooth video at 72 fps**: when the game renders at the video's frame rate (72 for 72, 90 for 90...), the recorder now takes each frame half a frame after the game publishes it. Before, if the game's frames arrived right around the scheduled instants, the slightest offset of a fraction of a millisecond made one frame be lost and the previous one repeated: up to one frame in four duplicated, and a stuttering video while the headset was smooth. Frame timestamps are unchanged: sound stays in sync. The recorder log shows "Prise calee sur les images du jeu".

## 1.13.0

- **Live on Twitch and YouTube at the same time**: "Service" *Twitch*, *YouTube* or *Both*. Each service has its own settings (key, format, resolution, bitrate) and its own encode: for example Twitch in H.264 1080p at 6000 kbps and YouTube in AV1 1440p at 18,000 kbps. The image is composed once, at the larger of the two sizes. If one service drops or refuses the key, the other keeps going; the Video page shows each one's status and bitrate. The frame rate (30 or 60 fps) is shared.

## 1.12.0

- **Live on Twitch and YouTube, without OBS**: the headset video (image, panel, game menus, filters, game sound and microphone) goes live, encoded in H.264 by the graphics card at a constant bitrate. Video page: "Output" *MP4 file* or *Live* (one or the other), service, stream key (kept encrypted for your Windows account, never shown again), resolution 720p to 1440p, 30 or 60 fps, bitrate (capped at 8000 kbps for Twitch). The video hotkey, the button or the tray menu starts the stream; **LIVE** and the duration show in the headset; upload rate, lost frames and reconnections on the Video page. Automatic reconnection for one minute after a drop. OpenXR and SteamVR games.
- **H.265 and AV1 for YouTube**: when live on YouTube, choose H.264, H.265 (HEVC) or AV1 ("Enhanced RTMP", like OBS): a finer image at the same bitrate. Twitch stays on H.264, the only format it accepts for a live stream.
- When live, the encoder never uses its slowest setting (*Very high* quality), so it keeps up in real time at 1440p60; recordings keep your choice.

## 1.11.0

- **OBS source for streaming**: OBS Studio (30 or later) gets an **"XR Perf (headset)"** source that receives the headset video image (eye, format, resolution, crop, stabilization, panel, filters) straight through the graphics card, with no double encoding. It runs as soon as the source is shown in OBS, without recording (no file, no sound, no REC indicator), and keeps going during a recording. OpenXR and SteamVR games. `Installer.bat` adds it to OBS if it is installed; setting and status on the Video page ("OBS source").
- **Game menus and HUD in the video** (OpenXR games): the flat screens the game has the runtime display over the scene ("quad" and "cylinder" layers: menus, interface) are now copied with the image and drawn in place, in perspective, in the recorded video as in OBS (up to four per frame).

## 1.10.0

- **Image filters** (new **Image** page): **sharpening** with *CAS* (AMD FidelityFX), *RCAS* (FSR 1) or *LumaSharpen*, with its strength; **colors**: brightness, contrast, saturation, vibrance, gamma, shadows (lifts dark areas while black stays black) and temperature; gradient **debanding**; **colorblind mode** (protanopia, deuteranopia, tritanopia). Three modes: *Off* (default), *In the headset* or *Video only*. In the headset (OpenXR games, Direct3D 11 and 12), the filters are applied last, after Pimax NR, OpenXR Toolkit or DLSS, and the recorded video gets them too. In a SteamVR game, they only apply to the video (*Video only* mode): SteamVR doesn't allow changing the game's image. `Ctrl+Alt+I` turns every filter off and back on at once, for the session, to compare. Settings apply immediately, even in game; a switch (Image page, "Headset filters possible") keeps the layer from ever touching the game's image.
- **Stronger sharpening when needed**: strength up to 200%, adjustable LumaSharpen **limit** (ReShade's "clamp") and a 1 to 3 pixel **reach** for supersampled games.
- **Fake HDR**: local contrast, like ReShade's FakeHDR (detail in bright and dark areas, deeper midtones).
- **Image presets**: *Reduced resolution*, *Natural crispness*, *Vivid colors*, *Fake HDR*, *Cinema*, *Game too dark*, *OLED*, and your own (save, replace, delete).
- **Every hotkey can be changed** (new **Hotkeys** page): any combination with Ctrl, Alt, Shift or Win (F1 to F24 alone), a "Default" button for each and one for all, duplicates with the video refused. Applies immediately, even in game, OpenXR and SteamVR. In `XRPerfOverlay.ini`, modifiers are written separately (`toggle_mods=6`); old files remain valid (Ctrl+Alt).

## 1.9.1

- Video: only the recorded eye's image is copied, instead of the whole image sent by the game. When the game puts both eyes in the same image (Metro Awakening, SteamVR's mirror image...), the copy takes half the work and video memory while recording. Dynamic resolution is still supported.

## 1.9.0

- **Headset video**: records what you see in the headset, without OBS. One eye's image, framed and centered on the direction of the head (canted displays included), is encoded by the graphics card in **H.264, H.265 (HEVC) or AV1** into an MP4 file, with optional **game sound** and **microphone** (output and microphone of your choice). Resolution from 720p to 2160p, 30 to 90 frames per second; *Light* to *Very high* quality, which picks the bitrate from the codec, resolution and frame rate (or bitrate by hand, 2 to 150 Mbit/s); eye, aspect ratio and framing adjustable. In an OpenXR game, the **panel** can be added to the video, in place as in the headset or in a corner. Optional light or strong **stabilization** and **level horizon**: a virtual camera follows the head smoothly, with no change in the headset. `Ctrl+Alt+V` (key of your choice), a button on the new **Video** page, or the tray icon menu start and stop recording; "VID" and the duration show at the top of the panel in the headset. OpenXR games in Direct3D 11 and 12 (through the layer) and SteamVR games (through XRPerfVR). Everything stays on the graphics card, nothing runs when not recording, and a switch turns the feature off.

- **Image recorded after the other layers**: with Pimax NR (DLSS), OpenXR Toolkit..., the video shows the processed image, as the headset receives it (or, if you prefer, the game's).
- Configuration page: **Remove the entry** of an uninstalled layer whose file no longer exists (uninstall leftover).
- **Video hotkey of your choice**: any combination (Ctrl, Alt, Shift, Win + a key, or an F key alone), entered with the keyboard on the Video page.
- **Power in watts** (option, Appearance page): the last column of the panel's CPU / GPU table shows the power of the graphics card and processor instead of their clock, in the headset (OpenXR and SteamVR) and on the desktop mirror. The XRPerfSensors dashboard shows it too.

## 1.8.0

- **Classic and EVO themes** for the XRPerfSensors window (Interface page). They also change the look: Classic, grey with rounded cards, a green accent and orange titles; EVO, midnight blue with sharp corners, uppercase titles and an indigo accent.
- Their colors for the panel, watch and mirror too (Appearance page, or `theme=classic` / `theme=evo` in the `.ini`). With "same as the panel", the window follows, look included.
- **OpenXR layer order** on the Configuration page: Move up / Move down arrows on each layer. Registrations are rewritten in the new order without losing anything (a rewrite cut short is finished at the next start). "Undo last change" puts back the state from before the last change of order, or of turning on or off.
- New finding: a frame generation tool placed above XR Perf Overlay (the panel's big number would also count generated frames).

## 1.7.2 (2026-10-21)

- Capped frame rate: also recognized when the cap does not divide the headset refresh rate (a game locked at 60 on a 72 Hz headset). The rate of each second varies there (50 to 58 at the lowest), which 1.7.1 missed; the cap is then recognized by the game waiting inside each frame (CPU time close to a frame's duration, graphics card under 90 %) over at least two minutes.
- When the game caps itself at times, the analysis no longer attributes the remaining drops to "specific moments" or to the graphics card (the remaining minutes often mix menus and capped stretches), and no longer gives a numbered resolution advice, which would rest on the menus.
- Game capped at times: the simple reading suggests manual recording (`Ctrl+Alt+S`) for a report on a single kind of stretch, and comparing two sessions warns that the gap mostly depends on the time spent in locked and free stretches, not on performance.

## 1.7.1 (2026-10-21)

- **Capped frame rate recognized in reports**: when the game limits itself (for example a fighting game locked at 60 frames per second on a 90 Hz headset, or cutscenes at 30), minutes stuck at a round value below the target are spotted. The report explains it ("a limit of the game, not a lack of power"), sets those minutes aside to judge what is limiting, and no longer wrongly blames the processor: at the cap, the game waits inside each frame and its CPU time swells. If almost the whole session is capped, it says the target is out of reach whatever the settings and advises a headset refresh rate equal to the cap, if one exists. A frame rate locked at an exact fraction of the target (45 for 90) is reported separately: it may also come from the headset's reprojection.
- Spotting works minute by minute: a capped stretch of a few seconds, mixed with normal play in the same minute, is not recognized.

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
