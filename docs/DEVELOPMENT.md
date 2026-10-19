# XR Perf Overlay: development

*Français : [DEVELOPPEMENT.md](DEVELOPPEMENT.md)* · [Back to the home page](../README.en.md)

Building from source, building and publishing the package, code layout. To simply use XR Perf Overlay, see the [home page](../README.en.md).

## The programs

| Folder | Role |
|---|---|
| `layer/` | The OpenXR *API layer* (C++ DLL) loaded into the game: measures FPS and displays the panel. |
| `sensors/` | `XRPerfSensors.exe` (C#), run separately: reads the PC's sensors and passes them to the layer; its window (interface in `sensors/ui/`) configures the overlay and shows the reports. |
| `layer/src/vr_*.cpp` | `XRPerfVR.exe` (C++), started by XRPerfSensors: panel, watch and reports in SteamVR (OpenVR) games. |
| `layer/src/window_capture.cpp` | `XRPerfCapture.dll` (C++), loaded by XRPerfSensors: captures the window shown in the headset. |

## Two ways to get it

- **Distribution package** (`XRPerfOverlay-<version>-win64.zip`, "Releases" page of the repository): the program already built. Nothing else to install: no Visual Studio, no SDK, no .NET runtime (included). Extract the archive, double-click `Installer.bat`. The package's `README-EN.txt` file guides the user.
- **Sources** (this repository): `Installer.bat` builds first, which needs the tools below.

Either way, the PawnIO driver is optional (CPU temperature and clock only).

## Requirements (building from source)

- Windows 10 or 11, 64-bit
- Visual Studio 2022 (or later) with the **"Desktop development with C++"** and **".NET desktop development"** workloads (without the second, the .NET SDK alone is enough: `winget install Microsoft.DotNet.SDK.10`). Without a .NET SDK, `build.bat` still builds the layer, but `Installer.bat` stops: it only installs the complete set.
- For the **CPU** temperature and clock: the **PawnIO** driver, installed once (`winget install namazso.PawnIO`, or https://pawnio.eu). It is the driver LibreHardwareMonitor uses since version 0.9.5. Without it, everything else works and these two values show `--`.
- An internet connection for the first build (NuGet downloads LibreHardwareMonitorLib and WebView2).

## Installing by hand (without Installer.bat)

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
  README.md, CHANGELOG.md, docs\   home page, changes, guide and screenshots (in French and English)
  LICENSE.txt, THIRD-PARTY.md
  scripts\setup.ps1
  programme\              what is copied to the installation folder (DLL, .json, .ini, VERSION,
                          install / uninstall, sensors\ with the .NET runtime)
```

**Automatic build (GitHub Actions).** The `.github/workflows/build.yml` file builds the layer with MSVC and XRPerfSensors on a Windows machine at each push, runs the tests in `tests/preview.cpp`, builds the package with Windows PowerShell 5.1 and attaches it to the run (Actions tab, Artifacts). To publish a version: put the number in `VERSION`, commit, then `git tag v1.0.0` and `git push origin v1.0.0`. The workflow checks that the tag matches the `VERSION` file and creates a **draft** release with the package: review it and publish it from the "Releases" page.

**Signing.** The files are not signed: Windows shows "unknown publisher" the first time `Installer.bat` runs, which `README-EN.txt` explains. The installer then removes the "came from the internet" mark from the installed files, so XRPerfSensors doesn't ask for confirmation at each start.

**License.** The project's code is under the MIT license (`LICENSE`). Third-party components and their licenses are in `THIRD-PARTY.md`.

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
