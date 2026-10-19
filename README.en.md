# XR Perf Overlay

*Français : [README.md](README.md)*

A performance panel shown in the headset for every OpenXR game (including games run with UEVR), and also for SteamVR games that don't go through OpenXR (OpenVR):

- **FPS**: instantaneous, average (AVG), minimum and maximum
- **CPU**: load, frame time, temperature, clock
- **GPU**: load, frame time, temperature, clock, VRAM

Optional: a wrist watch, and a desktop window (a stream chat, Discord...) shown in the headset.

![Panel preview](docs/apercu.png)

The project contains these programs:

| Folder | Role |
|---|---|
| `layer/` | The OpenXR *API layer* (C++ DLL) loaded into the game: measures FPS and displays the panel. |
| `sensors/` | `XRPerfSensors.exe` (C#), run separately: reads the PC's sensors and passes them to the layer; its window (interface in `sensors/ui/`) configures the overlay and shows the reports. |
| `layer/src/vr_*.cpp` | `XRPerfVR.exe` (C++), started by XRPerfSensors: panel, watch and reports in SteamVR (OpenVR) games. |
| `layer/src/window_capture.cpp` | `XRPerfCapture.dll` (C++), loaded by XRPerfSensors: captures the window shown in the headset. |

## Why it doesn't get in the way of mods

- The layer uses OpenXR's official mechanism (a DLL declared in the registry, inserted by the loader between the game and the runtime). It injects nothing, hooks no Direct3D or DXGI function and creates no window. By default it doesn't touch the controllers; only attaching to a hand (wrist watch, panel on a hand), if you choose it, adds the hand positions (see "Watch and attaching to the controllers").
- It intercepts only six OpenXR functions and changes only one: at `xrEndFrame`, it adds **one quad layer at the end** of the list sent by the game (two if the watch is shown). The game's or UEVR's images, poses and settings pass through unchanged. A frame the game submits empty (loading) stays empty.
- The only graphics call is a copy of the panel (640 × 320 pixels at most, four times per second) to a texture owned by the layer. No game rendering state is read or changed. The panel is drawn in memory and costs about a tenth of a millisecond per update. The only exception, and only if you turn it on: real GPU time measurement adds two timestamps per frame to the game's GPU work (see "GPU time: estimate or measurement").
- If the runtime rejects a frame because of our layer, the layer resubmits it right away without it; on the third rejection it disables itself for the session. At the slightest problem on its side, it steps aside.
- Sensors are read in a separate program: nothing risky (driver, hardware access) runs inside the game.
- It can be turned off with one click, without uninstalling: the "OpenXR games (layer)" switch in XRPerfSensors (Panel page, dashboard or tray icon menu). When off, it is no longer loaded into OpenXR games at all (see "Turning the layer off").

## Two ways to get it

- **Distribution package** (`XRPerfOverlay-<version>-win64.zip`, "Releases" page of the repository): the program already built. Nothing else to install: no Visual Studio, no SDK, no .NET runtime (included). Extract the archive, double-click `Installer.bat`. The package's `README-EN.txt` file guides the user.
- **Sources** (this repository): `Installer.bat` builds first, which needs the tools below.

Either way, the PawnIO driver is optional (CPU temperature and clock only).

## Requirements (building from source)

- Windows 10 or 11, 64-bit
- Visual Studio 2022 (or later) with the **"Desktop development with C++"** and **".NET desktop development"** workloads (without the second, the .NET SDK alone is enough: `winget install Microsoft.DotNet.SDK.10`). Without a .NET SDK, `build.bat` still builds the layer, but `Installer.bat` stops: it only installs the complete set.
- For the **CPU** temperature and clock: the **PawnIO** driver, installed once (`winget install namazso.PawnIO`, or https://pawnio.eu). It is the driver LibreHardwareMonitor uses since version 0.9.5. Without it, everything else works and these two values show `--`.
- An internet connection for the first build (NuGet downloads LibreHardwareMonitorLib and WebView2).

## Installing and updating

Extract the whole archive (into Downloads, for example), then double-click **`Installer.bat`**. That's all, for a first installation as for an update. If Windows shows a warning because the file comes from the internet, choose "More info" then "Run anyway".

1. from the sources, it builds the layer and the sensors program (one to two minutes); from the distribution package, there is nothing to build;
2. Windows asks for administrator permission, needed to declare the layer to OpenXR (`HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit`);
3. the files are copied to the installation folder, the layer is registered and `XRPerfSensors` is restarted.

The installation folder is the existing installation's, if there is one (the one registered with OpenXR, or else one found in `C:\XRPerfOverlay` or `C:\XRPerfOverlay\dist`), otherwise `C:\XRPerfOverlay`. When updating, your settings (`XRPerfOverlay.ini`) and session summaries (`sessions\`) are kept; the `XRPerfOverlay.defaut.ini` file placed next to them shows the new version's options. The `sensors\` subfolder is replaced entirely. On first installation, the installer asks whether `XRPerfSensors` should start with Windows. The installer's messages follow the Windows language (French or English).

`XRPerfSensors` can stay running: the installer stops it and restarts it. Close the running VR game before starting `Installer.bat`, though: while an OpenXR game is open, the old DLL cannot be replaced, and the installer stops without changing anything. If the build fails, nothing is installed either; details are in `build.log`.

Once installed, the extracted folder (and the archive) can be deleted. One exception: if the archive was extracted right where the default installation goes (`C:\XRPerfOverlay`), the program is placed in its `dist\` subfolder and the installer says so; that folder must then be kept.

Options, from a command prompt: `Installer.bat -Target "D:\Tools\XRPerfOverlay"` to choose the folder (no trailing `\`; an existing installation's settings are carried over), `-AutoStart oui` or `-AutoStart non` (`yes` / `no` work too) to skip the question.

### By hand

`build.bat` only builds and gathers the result in `dist\`:

```
dist\
  XR_APILAYER_NOVENDOR_perf_overlay.dll    the layer
  XR_APILAYER_NOVENDOR_perf_overlay.json   its declaration for OpenXR
  XRPerfOverlay.ini                        the settings
  install.bat / uninstall.bat
  sensors\                                 the sensors program (XRPerfSensors.exe, its libraries and its interface)
```

This `dist\` folder is copied as is (including **its** `sensors\` subfolder, not to be confused with the source `sensors\` folder next to `build.bat`) wherever you want, preferably a folder you can write to, so the hotkeys can remember their state in the `.ini`. `install.bat` registers the layer there; `install.bat -AutoStartSensors` also makes `XRPerfSensors` start at sign-in, without a UAC prompt. Otherwise, start `sensors\XRPerfSensors.exe` before playing (icon in the notification area, right-click → Exit).

With UEVR, choose **OpenXR** as the runtime in the injector: in OpenVR mode, the game doesn't go through OpenXR and the layer isn't loaded.

Uninstalling: `uninstall.bat` in the installation folder, then delete that folder.

## Version and distribution

**Version number.** It is written in one place only, the `VERSION` file at the root (form `1.2.3`). The build carries it everywhere: the DLL's version resource (file properties, Details tab), XRPerfSensors (window, menu and icon tooltip), the layer's log ("OpenXR instance created" line), the header of each session report, and the `VERSION` file in the installation folder. If the installed DLL and XRPerfSensors don't come from the same version, XRPerfSensors says so at startup.

**Building the package on your PC.** Double-click `Creer-paquet.bat`: it builds (like `build.bat`), publishes XRPerfSensors as self-contained (.NET runtime included; NuGet needs internet to download it), then produces `release\XRPerfOverlay-<version>-win64.zip` and its SHA-256 checksum. The `scripts\package.ps1` script does the work; it checks that the DLL carries the `VERSION` number, that XRPerfSensors is self-contained and that the package contains everything the installer expects.

Package contents:

```
XRPerfOverlay-<version>\
  Installer.bat           install or update (nothing to build)
  LISEZ-MOI.txt           user guide in French (README-EN.txt: the same in English)
  README.md, README.en.md, docs\   this documentation, in French and English
  LICENSE.txt, THIRD-PARTY.md
  scripts\setup.ps1
  programme\              what is copied to the installation folder (DLL, .json, .ini, VERSION,
                          install / uninstall, sensors\ with the .NET runtime)
```

**Automatic build (GitHub Actions).** The `.github/workflows/build.yml` file builds the layer with MSVC and XRPerfSensors on a Windows machine at each push, runs the tests in `tests/preview.cpp`, builds the package with Windows PowerShell 5.1 and attaches it to the run (Actions tab, Artifacts). To publish a version: put the number in `VERSION`, commit, then `git tag v1.0.0` and `git push origin v1.0.0`. The workflow checks that the tag matches the `VERSION` file and creates a **draft** release with the package: review it and publish it from the "Releases" page.

**Signing.** The files are not signed: Windows shows "unknown publisher" the first time `Installer.bat` runs, which `README-EN.txt` explains. The installer then removes the "came from the internet" mark from the installed files, so XRPerfSensors doesn't ask for confirmation at each start.

**License.** The project's code is under the MIT license (`LICENSE`). Third-party components and their licenses are in `THIRD-PARTY.md`.

## Usage

| Shortcut | Action |
|---|---|
| `Ctrl + Alt + O` | Show / hide the panel |
| `Ctrl + Alt + P` | Change position (four corners, top, bottom) |
| `Ctrl + Alt + R` | Reset AVG / MIN / MAX |
| `Ctrl + Alt + Page Up` / `Page Down` | Enlarge / shrink the panel (hold to continue) |
| `Ctrl + Alt + Home` / `End` | Panel more opaque / more transparent, text included (hold to continue) |
| `Ctrl + Alt + F` | Show / hide the captured window |
| `Ctrl + Alt + G` | Place the captured window in front of you, where you are looking |
| `Ctrl + Alt + S` | Manual report mode: start / stop recording (see "Session reports") |

The shortcuts work even if the game window doesn't have focus, and settings changed in game (display, position, size, opacity) are remembered in `XRPerfOverlay.ini`. This commented file also lets you set the update rate, the keys and the list of programs to ignore.

## The XRPerfSensors window

Double-click the `XRPerfSensors` icon near the clock (or right-click → **Open XR Perf Overlay**) to open the program's window. A menu on the left leads to each page; every change applies immediately, including in a running game, and is saved to `XRPerfOverlay.ini`. `F11` switches to full screen (or the button at the top right), `Esc` leaves it.

- **Dashboard**: the panel as it appears in the headset (the running game's image, otherwise a preview with sample FPS), quick switches (OpenXR layer, panel, watch, desktop window, SteamVR games, mirror for OBS), the processor, graphics card and video memory live with their graph of the last two minutes, the last session and the keyboard shortcuts. A badge at the top shows whether a VR game is running.
- **Panel**: show or hide the panel, attachment (in front of the eyes, left or right hand); in front of the eyes: position (chosen on a diagram of the field of view), distance, horizontal and vertical offsets, turning towards the eyes; on the hand: position on the hand, show only when looking at the hand, position adjustments; size, overall and background opacity; in games: the OpenXR layer (see "Turning the layer off") and SteamVR (OpenVR) games. The preview stays alongside.
- **Appearance** (headset, watch and mirror): layout, colors (thumbnails of the seven sets), clock, headset / resolution line, session / battery line.
- **Watch**: show the watch, attachment, position on the hand, style, elements, show on gaze, size, opacity, position adjustments, with its preview (see "Watch and attaching to the controllers").
- **Window**: the desktop window shown in the headset (see "Desktop window in the headset").
- **Desktop mirror**: the mirror window for OBS, its position, size, opacity, keep on top.
- **Measurements**: real GPU time measurement (otherwise an estimate), writing reports (every session, on the `Ctrl+Alt+S` hotkey or never: applies immediately, even in game), processor and graphics card models in the reports, reports folder. GPU measurement and models apply the next time a game starts.
- **Reports**: see "Session reports".
- **Interface**: language (Windows', French or English), window colors ("same as the panel" in the headset, one of the six themes, or three colors of your own), text size, animations, full screen and opening in full screen.

The window is displayed by Edge's **WebView2** engine, which comes with Windows 11 and up-to-date Windows 10 (the installer warns if it's missing). When closed, it is destroyed and uses nothing; the program keeps running in the notification area.

**Language.** French or English for everything: the window and the icon menu, the panel and the watch in the headset, and the reports written after the change (the reports page reads both languages; the analyses are written in the chosen language, whatever the file's). By default, Windows' language: French for a French Windows, English otherwise. In the `.ini`: `language=auto`, `fr` or `en` (`[overlay]` section). The installer also speaks the Windows language.

Three layouts:

| Layout | Content |
|---|---|
| Full | FPS, AVG / MIN / MAX, CPU / GPU table (load, frame time, temperature, clock), VRAM with a gauge. |
| Graph | FPS, AVG / MIN / MAX, frame time graph of the last ten seconds, then CPU and GPU on two lines. |
| Compact | FPS, AVG / MIN / MAX only. |

In the graph layout, the colored curve is the time between two game frames (lower = faster), the thin light line the CPU time and the thin colored line the GPU time. The dotted line marks the frame budget, the time not to exceed to hold the target (13.9 ms at 72 Hz, 27.8 ms when frame generation caps the game at 36); the curve turns red when it goes over. The legend says "GPU (est.)" when the GPU line is the estimate: it then moves in steps, one value per panel refresh. With real measurement, it follows each frame.

Six color sets are offered (dark, light, high contrast, green, amber, violet), plus a custom set where you choose the background, text and accent. The XRPerfSensors window can use the same ones.

![Layouts and colors](docs/presentations.png)

**Preview without a game.** When the mirror window or the XRPerfSensors window is shown and no VR game is running, it shows a preview: the panel as it will look in the headset, with your settings and your real sensors, but sample FPS. The preview is drawn by the layer DLL itself, so by the same code as in the headset. As soon as a game starts, the real panel is shown; during loading the last game image stays, and the preview only returns once the game is closed.

Settings changed in XRPerfSensors and those changed in game with the keyboard complement each other: the last change wins, and the window rereads the file each time it comes back to the foreground.

The clock shows at the bottom right of the panel (`clock=0` in the `.ini` to remove it).

The last line of the panel gives the headset name and the render resolution per eye (`headset_info=0` to remove it; the panel then gets its original height back). It is the size of the image the game really sends to the headset. When it differs by at least 3 % from the resolution recommended by the runtime, the difference is shown next to it, for example `6268 x 5034 (x1.21)`: the game applies its own render scale (graphics option, `vr.PixelDensity` for an Unreal game, UEVR setting), on top of the supersampling set in the headset software. The session report records both resolutions and points it out in its findings. Under SteamVR, the recommended resolution follows SteamVR's resolution setting (automatic by default), not the headset software's. The name is the one the OpenXR runtime reports: depending on the runtime, it's not always the headset's commercial name (SteamVR gives its driver's name). To show yours, fill in `headset_name=` in the `.ini`, for example `headset_name=Pimax Crystal Light`. The name reported by the runtime is written to the log at each session.

Colors: the FPS is green when the headset rate is held, orange above half, red below. A frame time turns orange then red when it approaches then exceeds the frame budget (11.1 ms at 90 Hz). Temperatures turn orange then red (CPU 85 / 95 °C, GPU 80 / 87 °C).

### Turning the layer off

The **OpenXR games (layer)** switch (Panel page, dashboard, or the "OpenXR layer enabled" item of the tray icon menu) takes the layer out of OpenXR games without uninstalling it: XRPerfSensors sets its OpenXR registration to "disabled" (value `1` instead of `0` in `HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit`, the mechanism OpenXR provides). The OpenXR loader then ignores it completely: it is no longer loaded into games, as if it were not installed. Turning it back on sets the value to `0` again. The change applies the next time a game starts.

Writing to that key needs administrator rights, which XRPerfSensors already has (Windows grants them when it starts, to read the sensors): no other permission is asked. An update with `Installer.bat` keeps the layer off if it was. SteamVR (OpenVR) games have their own switch, "Also in SteamVR games": XRPerfVR doesn't depend on the layer, and can show the panel in an OpenXR game started through SteamVR while the layer is off.

## Watch and attaching to the controllers

![The three watch styles](docs/montres.png)

**The watch** is a second small layer, independent of the panel: the time, and optionally the session time, the controllers' battery (left and right) and the FPS (green when the rate is held). Three styles: classic with hands, digital, sport (numbers and a seconds ring). It uses the colors chosen on the Appearance page. It goes:

- **on the wrist** (left or right hand): on the back of the wrist like a real watch, on the inside of the wrist, or above the controller;
- **in front of the eyes**, at the bottom right, if you prefer, or if the game doesn't declare controllers.

With the **"Only when I look at my wrist"** option (on by default), the watch only appears when you turn your wrist towards you and it is in your field of view; the rest of the time, it takes no room. The face is flat, as on a real watch: a curved OpenXR layer (cylinder) is meant to be seen from inside and would display badly wrapped around a wrist.

**The performance panel** can also be attached to a hand (Panel page, "Attached to"), with the same positions and the same show-on-gaze option. In both cases, two sliders adjust the position (along the forearm, distance from the hand): the default positions are computed for common controller sizes and may need adjusting for yours.

**How the layer gets the hand positions.** OpenXR only provides them through "actions", the game's button mechanism. The layer therefore slips into three of the game's calls, removing nothing, as OpenXR Toolkit or OpenKneeboard do:

1. when the game declares the bindings of a controller model, it adds a single item to them, the grip pose of each hand;
2. when the game attaches its actions to the session, it adds its small set ("XR Perf Overlay");
3. likewise at each action update by the game.

It declares **no button**: it cannot take anything away from the game. It never adds a controller model the game didn't declare itself, so as not to change the one the runtime picks. If one of these calls fails with its addition, it is redone as the game provided it and attaching is abandoned for the session (the watch goes back in front of the eyes); the log says so. This mechanism is only set up if the watch is enabled or the panel is attached to a hand **when the game starts**: turned on mid-game, the game must be restarted. In SteamVR's controller binding screen, the "XR Perf Overlay" set and its hand pose may appear.

**The battery** comes from the brand-new OpenXR extension `XR_EXT_interaction_profile_battery_state_display`: the layer requests it from the runtime only if offered (and if the watch must show the battery). SteamVR provides it; other PC runtimes, such as Pimax OpenXR, not yet: without it, the battery simply doesn't show. In OpenVR games, XRPerfVR reads the battery directly from SteamVR.

## SteamVR (OpenVR) games: XRPerfVR

Many SteamVR games still use Valve's older interface, OpenVR, rather than OpenXR: the layer is never loaded there. For them, `XRPerfSensors` starts a small windowless program, **`XRPerfVR.exe`** (stored next to it, in `sensors`), which brings the same features:

- **the panel and the watch**, with all the settings (position, layout, colors, language, hand attachment, show on gaze, keyboard shortcuts, live changes from XRPerfSensors);
- **the controllers' battery**, read from SteamVR;
- **session reports**, in the same format (the reports page reads them with the others; their header says "OpenVR runtime");
- **the desktop window** (mirror for OBS).

**How it works.** XRPerfVR is a SteamVR "overlay" application, like fpsVR or OVR Advanced Settings: it doesn't touch the game. It waits for SteamVR to be started (it never starts it: it checks that its server is running before connecting), then reads the frame times SteamVR's compositor records for the game in the foreground. **GPU time is therefore measured** (by SteamVR), without an option or "~". The CPU time is the game's render thread time, from receiving the poses to submitting the frame. When SteamVR closes, XRPerfVR disconnects and waits for the next start.

**OpenXR games started through SteamVR.** SteamVR sees them like the others. The layer, when active in a game, signals it (two named objects specific to the game's process): XRPerfVR then leaves it to the layer, with no second panel or report. With an OpenXR game in Vulkan or OpenGL, where the layer measures without being able to display, XRPerfVR shows the panel and the layer writes the report.

**Limits.** In OpenVR, the game's real image size isn't visible: the panel line and the report give the resolution recommended by SteamVR ("recomm."). The graphics API isn't recorded. On a hand, the position follows the "grip" component of the controller model provided by SteamVR; if the model has none, the controller's tracking origin is used and the adjustment sliders help correct it.

Setting: "Also in SteamVR games (OpenVR)", Panel page (`[openvr]` section of the `.ini`). Valve's `openvr_api.dll` library (BSD license) is shipped next to the program.

## Desktop window in the headset

A window open on the PC, for example a stream chat in a browser or Discord, can be shown in the headset, read-only (no clicks or typing). It is set up in `XRPerfSensors`, **Window** page: choice of the window among the open windows, preview of what will be shown, position, size, opacity, frames per second (5 by default, enough for a chat) and crop (share removed from each side, to keep for example only the chat column of a page).

**Position.** Two options:

- **placed in the room** (default): the window stays at one spot in the room, and you can turn your head without it moving. `Ctrl+Alt+G` places it in front of you, where you look, at the chosen distance (0.80 m by default), facing you and upright. Its position is saved in `XRPerfOverlay.ini` and reused from one game to the next; as long as it has never been placed, it is placed in front of you when first shown. It is fixed in the room's reference space (STAGE in OpenXR, SteamVR's own): a view recenter by the game moves it along;
- **on a hand**: like the panel, with the same positions (above the controller, it rests on its bottom edge), the same adjustment sliders and the "only when I look at my hand" option. Controller tracking is set up when the game starts; if the game doesn't declare controllers, the window is placed in the room.

**How it is captured.** `XRPerfSensors` loads `XRPerfCapture.dll`, which uses Windows' capture API (the one used by OBS and Discord, Windows 10 version 1903 or later). Capture only runs if someone wants the image: the layer or XRPerfVR when a VR game shows the window (they signal it in shared memory), or the preview on the Window page; it stops a few seconds later. The image is cropped, scaled down if it exceeds 1280 x 1280 pixels, then copied to the headset at each change of the window, at the chosen rate: nothing is copied while it doesn't change. In OpenXR games, the layer displays it (a third layer, under the panel and the watch); in SteamVR (OpenVR) games, XRPerfVR does.

**Limits.** A minimized window is no longer drawn by Windows: the last image stays shown until it is reopened (it can stay behind other windows). Windows may draw a yellow border around the captured window during capture: it does on Windows 10; on Windows 11, XRPerfCapture asks to do without it. Some copy-protected videos show black. The text of an ordinary-sized window is small in the headset: enlarging the chat font (or zooming the page) and the window width in the settings helps a lot.

## GPU time: estimate or measurement

By default, the GPU time per frame is **estimated**: card load (read by `XRPerfSensors`) × frame duration. It shows with a leading "~" (`~9.4 ms`). Nothing is added to the game's rendering, but the value counts everything the card does (headset compositor, frame generation, other programs), smooths spikes over about a second, and is always roughly the frame duration when the card is saturated.

The **"Measure the real GPU time"** option (Measurements page, or `measure=1` in the `[gpu]` section of the `.ini`) times each frame on the card itself: the layer places a GPU timestamp at the start of the frame (`xrBeginFrame`) and one at the end (`xrEndFrame`), then reads the result a few frames later, never waiting for the card. The measured time shows without "~" and replaces the estimate in the panel, the graph and the report; the estimate stays in the report for comparison. It applies the next time a game starts.

- Direct3D 12: two small command lists per frame, sent on the game's queue.
- Direct3D 11: timestamp queries on the game's context. If the game enabled Direct3D 11 multithread protection, the start marker is placed at `xrBeginFrame`; otherwise, so as never to touch the context from a thread other than the one submitting frames, it is placed at the end of the previous frame, and the measurement then runs from the end of one frame to the end of the next.
- Vulkan and OpenGL: no measurement, the estimate stays.

What the measurement represents: the time, on the card, between the start and the end of a game frame's work. When the card is the bottleneck, it is the time it spends on the frame. When the processor is, the card waits part of that time and the measurement counts it too: a measured GPU time close to the budget doesn't prove the card is saturated, its load tells. The cost is negligible (a few microseconds per frame). If the driver returns no result, the log says so and the estimate stays.

**Games whose rendering escapes the measurement.** The markers only measure work sent on the command queue the game declared to OpenXR, between its `xrBeginFrame` and `xrEndFrame` calls. Some games submit their rendering elsewhere or later: Unreal Engine 5 in Direct3D 12 submits it from a separate thread, and with UEVR it's UEVR that opens the OpenXR session and only copies the image between those two calls. The measurement then drops to a few tenths of a millisecond for a card at 90 % load, which is impossible. The layer detects it: if the measured time stays under 30 % of the estimate for 10 seconds while the card is at 50 % load or more, measurement is stopped for the session (no more markers), the estimate takes over everywhere, the log says so and the report explains it. The reports page applies the same rule to reports written before this fix. Measuring that rendering anyway would require intercepting the game's Direct3D command submissions, which the layer refuses to do so as not to get in the way of mods.

## Panel on the desktop (recording with OBS)

The headset panel is a layer added by the runtime: it doesn't appear in the game window or in UEVR's mirror. To have it in a recording without an OpenXR mirror (costly in FPS), `XRPerfSensors` can show it in a small desktop window, to add in OBS as a second source:

1. Right-click the `XRPerfSensors` icon (notification area) → check **"Panel mirror on the desktop (OBS)"**, or turn it on on the Desktop mirror page. A borderless "XR Perf Overlay" window appears; it can be moved with the mouse.
2. In OBS, capture the game as usual (game capture or window capture of UEVR's mirror), then add a **"Window Capture"** source on the `XR Perf Overlay` window, and place and resize it over the game image.
3. If the source stays black: `XRPerfSensors` runs as administrator, so OBS may need to run as administrator too, or the source's capture method may need changing.

The Desktop mirror page offers its size (25 to 300 %), its opacity, its position (free, or snapped to a screen edge) and "Keep on top": it then stays visible over the game on screen, which is enough for a full-screen capture. Position, size and choices are remembered.

The window always shows the panel with an opaque background, even if it is hidden or made transparent in the headset; its own opacity is set separately. It takes the panel's height, depending on the chosen layout. The layer and `XRPerfSensors` must come from the same version, which `Installer.bat` takes care of; with a DLL and an `XRPerfSensors` of different versions, the window keeps waiting. On the game side, the layer only copies the panel image while the window (or the XRPerfSensors window) is shown (about 0.15 ms four times per second); otherwise there is no extra work.

## Session reports

At each game session (or, in manual mode, at each recording: see below), the layer writes a report (text file) in the `sessions\` subfolder (next to the DLL), named after the date, time and game, for example `2026-10-10_06-50-12_Game-Win64-Shipping.txt`. It contains:

- the headset, the OpenXR runtime and its version, the render resolution per eye (and its extremes if the game changed it during the session);
- the processor model and the graphics card model (option, on by default): the processor as Windows names it, and the card the game really renders on (its Direct3D device's; with Vulkan or OpenGL, the card with the most video memory). The layer reads them itself; XRPerfSensors isn't needed. Turn it off on the Measurements page or with `hardware=0` in the `[summary]` section of the `.ini`, for example before sharing reports;
- the session duration, the number of frames rendered and the number of interruptions (loading, pauses);
- FPS: average, minimum and maximum over one second, 1 % low and 0.1 % low, and the share of time the target was held;
- frame times (whole frame, CPU, measured GPU if the option is on, estimated GPU), average and maximum;
- for the processor and the graphics card: load, temperature and clock, average and maximum, plus VRAM;
- the distribution of frame times against the budget: share of frames within the budget, up to 1.5 times, 1.5 to 2 times, beyond;
- a minute-by-minute log: average and minimum FPS, CPU and GPU times, loads, temperatures, VRAM. With real measurement, a minute without measurement uses the estimate, with a leading "~".

The report is written in the language chosen at the time of writing (see "Language"). The file is rewritten every 15 seconds while playing, then a last time at the end of the session. If the game crashes or is closed abruptly, the file keeps the last update ("Last update" line instead of "End"). A session under 15 seconds leaves no file. The summary is produced even when the panel is hidden, and also for Vulkan or OpenGL games, where the panel itself isn't shown.

**When to write a report.** Measurements page, "Write a report" (or `mode=` in the `[summary]` section of the `.ini`):

- **Every session** (`auto`, default): one report from game start to exit, as described above.
- **On hotkey** (`manual`): nothing is written until you ask for it. `Ctrl+Alt+S` starts a recording: a red dot and the duration ("REC 01:23") show next to the target, at the top of the panel. `Ctrl+Alt+S` again stops it and writes the report, which covers only that stretch ("OK" in green for a few seconds); under 5 seconds leaves no file ("< 5 s" in orange). Handy to compare the same stretch with different settings. A recording still running when the game closes is ended and kept. The report carries the line "Recording: manual", and the Reports page shows it. The key is changed with `record=` in the `[hotkeys]` section.
- **Never** (`off`): no report; the panel is still measured and shown normally.

The mode applies immediately, even in game: switching from "every session" to another mode ends the current report; switching back to "every session" starts a new one. It also applies in SteamVR games (XRPerfVR), where the hotkey and the indicator work the same way. The hotkey is read by whoever writes the report: in an OpenXR game in Vulkan or OpenGL started through SteamVR, that's the layer, which passes its indicator to the panel shown by XRPerfVR. Switching to "never" mid-session keeps the report already started if it covers at least 15 seconds.

Sensor values need `XRPerfSensors.exe` to be running. The "1 % low" is the rate matching the frame time that 99 % of frames didn't exceed. The folder is changed in the `[summary]` section of `XRPerfOverlay.ini` (full path). If the folder isn't writable, reports go to `%LOCALAPPDATA%\XRPerfOverlay\sessions`.

**Reports page.** Right-click the `XRPerfSensors` icon → **Session reports** (or the Reports page of the window). On the left, the list, grouped by day, filterable by game and by search, gives for each session a colored dot (verdict), the average FPS, the duration and the share of time the target was held (an ochre dot: session in progress or game closed abruptly). The keyboard arrows move from one session to the next. On the right, four views:

- **Simple**, for those who don't know the vocabulary: a colored verdict ("Very smooth", "Smooth, with a few hitches", "Decent, but not always smooth", "Often stuttering") with a ring showing the share of time the target was held, then each explanation illustrated: what you may have felt, with a grid of 100 frames (on time, a little late, more than twice the expected time); what held it back, with two bars (the processor's time to prepare a frame against the budget, the share of time the graphics card was busy), the busiest one marked "bottleneck"; temperatures and video memory, with gauges and their risk zone; the comparison with the previous session, in bars; concrete leads and a small glossary.
- **Detailed**: findings drawn from the numbers (target held or not, what seems to limit, hitches, temperatures, video memory, frame rate dropping over the session, resolution changed by the game, difference with the previous session of the same game), then all the numbers and the frame time distribution as a bar. These are clues drawn from averages, not a diagnosis.
- **Comparison**: two sessions side by side. By default, the selected session and the previous one of the same game; Ctrl + click to choose two. A table of key numbers with, for each, a green (A does better) or red (A does worse) difference bar, gray for loads and memory, which are neither better nor worse in themselves; the two 100-frame grids; and the curves of A and B overlaid (FPS, CPU time, GPU time, graphics card load and temperature, video memory). If the two reports show different hardware or settings (render resolution, headset, runtime, graphics API), the page says so: a session rendered with 30 % more pixels doesn't compare directly with another.
- **Text**: the file as is.

In the simple or detailed view, **the minute-by-minute curves are on the same page as the analysis**: beside it on a wide screen, below it in a narrow window. Five graphs: FPS (average and minimum, with the target), CPU and GPU times (with the budget), loads, temperatures, video memory. On hover, a vertical line follows the mouse and gives the minute's values; with the keyboard, the arrows do the same.

The buttons open the report in Notepad or its folder in Explorer, and **Delete** (or the Delete key) moves the selected reports to the Recycle Bin, after confirmation. On a drive without a Recycle Bin (network drive, USB stick), Windows deletes them permanently.

The file stays open for the whole session and each update happens in the system cache: no disk access takes place in the render loop.

## What each value measures

| Value | Definition |
|---|---|
| with FG | Shown only with a frame generation tool: frames sent to the headset per second, generated ones included (repeats of the same frame don't count). The big number stays the game's real rate. See "Frame generation" below. |
| FPS | Frames the game submits to the runtime per second (`xrEndFrame`), averaged over 1 s. It is the game's rate, not the display's: in reprojection, the headset shows more frames than the game produces. |
| target | Rate requested from the game at that moment (frame display duration given by `xrWaitFrame`). Usually the headset refresh rate, but a fraction of it when a tool caps the game: with 2X frame generation on a 72 Hz headset, the target is 36. |
| AVG | Average over the sliding window (30 s by default). |
| MIN / MAX | Lowest and highest "1 second" FPS in the window. An interruption longer than a second (loading, pause) resets the window instead of counting as a drop. |
| CPU frame time | Time between the return of `xrWaitFrame` and the call to `xrEndFrame`: what the game takes to make a frame (simulation + render preparation). When the GPU is saturated, this time can include waiting on the GPU. |
| GPU frame time | By default, an **estimate** (shown with "~"): GPU load × frame duration. It needs `XRPerfSensors`, smooths spikes over about a second and counts all of the card's work. With the real measurement option (Direct3D 11 and 12), the time between two GPU timestamps placed at the start and end of each game frame, averaged between two panel refreshes. See "GPU time: estimate or measurement". |
| CPU load / temp. / clock | Total load over all cores; package temperature (Intel) or Tctl/Tdie (AMD); clock of the fastest core at that moment. |
| GPU load / temp. / clock | The driver's "GPU Core" sensors (NVIDIA, AMD or Intel). With several cards, the one with the most video memory is followed. |
| VRAM | Video memory used / total on the card, all programs included. |
| Headset | System name reported by the OpenXR runtime (`xrGetSystemProperties`), or the one chosen with `headset_name=`. |
| Resolution "per eye" | Size, in pixels, of the image the game submits to the runtime for the first eye at each `xrEndFrame`. It is the resolution the game really renders at, after its own settings (resolution scale, DLSS...): it usually differs from the headset displays' resolution, because the runtime asks for a larger image to compensate for lens distortion. If another tool enlarges the image after the layer (scaling placed lower in the chain), the size before that tool is shown. With quad-views rendering, it is the peripheral view. |

## Compatibility and limits

- **Direct3D 11 and Direct3D 12** games (both UEVR cases). With Vulkan or OpenGL, the layer lets everything through without showing a panel; only the session report is produced.
- In Direct3D 11, the texture copy goes through the game's context, from `xrEndFrame`. This assumes the game calls `xrEndFrame` on its render thread, as runtimes and other overlays already assume; it is the case of Unreal Engine with UEVR.
- **Frame generation (OFXR Bridge, etc.)**: the layer sits between the game and the generation tool. The big number is therefore the rate of frames really rendered by the game (36, for example), and the target follows what the tool asks of the game. To also show the rate after generation ("72 with FG"), the layer adds, when the game creates its OpenXR instance, a second link at the very bottom of the chain, just before the runtime: it only counts what the runtime receives (three functions passed through unchanged). This link is only added if a known generation tool is detected; if a layer refuses the longer chain, the instance is recreated without it. Setting `measure_output` (`[framegen]` section of the `.ini`): `auto`, `1` or `0`. The log lists the layers found below ours and whether the measurement is active. GPU load and frame time include the cost of generation.
- Any OpenXR runtime that supports quad layers, which is part of the core standard (Pimax, SteamVR, Meta, WMR...).
- **OpenVR** games: through XRPerfVR, when SteamVR is running (see "SteamVR (OpenVR) games").
- The layer is loaded into every OpenXR program. To keep it out of a program, add its executable to `exclude=` in the `.ini`.
- Games protected by anti-cheat may refuse API layers, like any tool loaded into the game.

## Verification status

What was checked before delivery, and what wasn't:

- The C++ layer compiles and links for 64-bit Windows (clang / MinGW-w64 cross build, warnings treated as errors).
- Its OpenXR logic was run against a fake loader and a fake runtime (normal loop, hotkeys, swapchain image not ready, layer rejected by the runtime, session destroyed and recreated, Vulkan session, excluded program), under AddressSanitizer. The watch, hand attachment, captured window and XRPerfVR (against a fake SteamVR) go through the same test benches.
- Panel drawing (French and English), statistics and session reports (French and English) are tested (`tests/preview.cpp`).
- Report reading and analysis (`sensors/SessionReport.cs`) and their formatting for the interface (`sensors/ReportJson.cs`) are run as is on French and English reports produced by the C++ code.
- The C# program is compiled with the Roslyn compiler against Windows Forms, LibreHardwareMonitorLib 0.9.6 and the WebView2 SDK. The settings block written by C# is read back by C++, and the off-game preview is called from the C# code on the compiled library.
- The interface (`sensors/ui/`) runs in Chromium, WebView2's engine, with a simulated program (`sensors/ui/dev/mock.js`) feeding it real data: panels and watches drawn by the layer, reports analyzed by the real C# code. Every page was viewed in French and English, in dark and light themes, at large and small sizes. The 1.5 WebView2 window (`MainWindow.cs`) was used on a real Windows 11 PC.
- Manual report mode (1.6): the decision logic (mode, hotkey, mode change in game, indicator) is tested in `tests/preview.cpp`; both test benches (layer and XRPerfVR) start and stop recordings with the hotkey (report kept, too short, ended at session end, mode changed live). The indicator is drawn in the three layouts. Not yet tried in game.
- Turning the layer on and off (1.6, `LayerRegistry.cs`): compiled and reviewed, not yet tried on Windows (registry write, update keeping the layer off).
- The package and installation scripts are run with stand-ins for `dotnet`, the registry, scheduled tasks and elevation, in French and English.
- Real-world test: the layer built with Visual Studio (MSVC) works in game on a Pimax headset, alongside OFXR Bridge and other mods, through Pimax OpenXR as through SteamVR. XRPerfSensors (its 1.4 windows, then the 1.5 window), the desktop panel, the watch, hand attachment, the battery (under SteamVR), the session / battery line, XRPerfVR (OpenVR games) and the captured window were used in game.
- **Not checked**: other runtimes (Meta, Virtual Desktop, Windows Mixed Reality...) and headsets from other brands. Real GPU time measurement has not been confirmed on a real card. The name each runtime reports for each headset hasn't been recorded: only the log will tell what yours returns.

## Troubleshooting

- **Log**: `%LOCALAPPDATA%\XRPerfOverlay\XRPerfOverlay.log`. One line per game start says whether the overlay is ready and, if not, why; the session report's path is noted there too. The log is written in French.
- **Nothing shows**: check that the game really uses OpenXR (an "Instance OpenXR créée" line must appear in the log), that the panel isn't hidden (`Ctrl+Alt+O`), then reduce `offset_x` / `offset_y` if it goes out of view.
- **"No sensors"** in the panel: `XRPerfSensors.exe` isn't running.
- **The watch stays in front of the eyes instead of the wrist**: controller tracking is only set up when the game starts; restart it. The log says "Suivi des manettes actif" (controller tracking active), or why it isn't (game without declared controllers, refused by the runtime). Under SteamVR, if tracking is active but the watch never appears on the wrist, the controller configuration used for this game (personal or community) may not bind the "XR Perf Overlay" set's hand pose: go back to the game's default configuration, or bind it in the controller binding screen.
- **No battery on the watch**: the runtime doesn't offer the battery extension.
- **Nothing in a SteamVR (OpenVR) game**: the "Also in SteamVR games" option must be on and XRPerfSensors running. The log says "XRPerfVR : connecte a SteamVR", then the followed game, or why it isn't followed (excluded in the `.ini`, OpenXR game left to the layer).
- **Frozen or flickering panel in a SteamVR (OpenVR) game**: the log reports the layer image refused by SteamVR with a code, then accepted again if things settle.
- **The captured window doesn't appear**: the Window page of XRPerfSensors gives the capture state (window not found, minimized, Windows too old, capture refused). In a game, "Show the window in the headset" must be on (or `Ctrl+Alt+F`); placed in the room, it may be behind you: `Ctrl+Alt+G` brings it in front. A window started as administrator may refuse to be captured by XRPerfSensors if it isn't.
- **The GPU time keeps its "~" despite the measurement option**: the option applies the next time the game starts; the log then says whether measurement is on, or why not (Vulkan, OpenGL, driver returning nothing).
- **`--` for CPU temperature or clock**: PawnIO isn't installed, or the processor isn't recognized by LibreHardwareMonitor.
- **The XRPerfSensors window stays empty or says WebView2 is missing**: install Microsoft's WebView2 engine (https://developer.microsoft.com/microsoft-edge/webview2/). The rest of the program works without it.
- **Disable without uninstalling**: the "OpenXR games (layer)" switch in XRPerfSensors (see "Turning the layer off"), or the environment variable `DISABLE_XR_APILAYER_NOVENDOR_perf_overlay=1`.
- **Nothing in an OpenXR game, "layer is off" banner in XRPerfSensors**: the layer was turned off; turn it back on in the same place, then restart the game.
- **No report**: check the mode (Measurements page). In manual mode, the recording must be started then stopped with `Ctrl+Alt+S`; the log notes each start and end of recording.
- **A game behaves oddly**: run `uninstall.bat` to remove the layer, and look at the log.

## Code layout

```
layer/src/layer.cpp           negotiation with the OpenXR loader, intercepted functions
layer/src/overlay.cpp         per-session logic: measurements, swapchain, quad layer, hotkeys
layer/src/gfx_d3d11.cpp       panel copy to the texture, GPU time measurement (Direct3D 11)
layer/src/gfx_d3d12.cpp       same (Direct3D 12)
layer/src/panel.cpp           panel and watch drawing in memory (layout, colors, French / English text)
layer/src/hand_input.cpp      controller positions (added to the game's actions) and battery
layer/src/report_control.h    when to write a report: automatic, manual (Ctrl+Alt+S, REC indicator) or never
layer/src/session_stats.h     whole-session totals and text report formatting, French or English (read by SessionReport.cs)
layer/src/vr_host.cpp         XRPerfVR: OpenVR games (waiting for SteamVR, sessions, measurements, layers, reports)
layer/src/window_capture.cpp  XRPerfCapture.dll: window capture (Windows capture API) to shared memory
layer/src/settings_apply.h    bounded application of the settings sent by XRPerfSensors (including the language)
layer/src/shared_block.h      shared memory format (must stay aligned with sensors/*.cs)
sensors/Program.cs            sensor reading, notification icon and its menu
sensors/MainWindow.cs         main window: WebView2, messages with the interface, previews, sensors, full screen
sensors/OverlaySettings.cs    headset settings: .ini reading / writing, sending to the layer, preview drawn by the layer
sensors/SettingsJson.cs       settings exchanged with the interface (JSON) and their bounds
sensors/ReportStore.cs        reports: folder reading, previous session, opening, deleting
sensors/ReportJson.cs         reports formatted for the interface (analyses in the chosen language)
sensors/SessionReport.cs      reading (French or English) and analysis of reports, "simple" reading in sentences
sensors/LayerRegistry.cs      turning the layer on or off in the OpenXR registry (elevated relaunch "--layer on|off")
sensors/Loc.cs                program text language (Windows', French or English)
sensors/PanelWindow.cs        desktop mirror of the panel (OBS, off-game preview) and PC-specific settings
sensors/WindowCapture.cs      window choice, shared memory, capture control
sensors/VrCompanion.cs        starting, restarting and stopping XRPerfVR
sensors/ui/                   interface: index.html, app.css, app.js (pages), charts.js (graphs), i18n.js (FR / EN texts), fonts
sensors/ui/dev/               simulated program to develop the interface in a browser (not shipped)
tests/preview.cpp             statistics, panel, report and language tests (cmake -DXRPO_BUILD_TESTS=ON)
scripts/setup.ps1             copy to the installation folder, registration, restarting the sensors
scripts/package.ps1           package assembly (self-contained XRPerfSensors, archive, checksum)
docs/LISEZ-MOI.txt            user guide shipped in the package (docs/README-EN.txt: in English)
```

To change the panel's look, everything is in `panel.cpp`; the preview shows the result without starting a game. To change the interface, open `sensors/ui/index.html` in a browser: the simulated program in `sensors/ui/dev/` stands in for XRPerfSensors.

Third-party licenses: see `THIRD-PARTY.md`.
