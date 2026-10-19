XR PERF OVERLAY
===============

A performance panel in the headset for OpenXR games (including games run with UEVR) and SteamVR
(OpenVR) games: FPS, CPU and GPU frame times, load, temperature and clock of the processor and
graphics card, video memory. A report is written for every game session.


WHAT YOU NEED
-------------
- Windows 10 or 11, 64-bit.
- An OpenXR game in Direct3D 11 or 12 (Vulkan and OpenGL: no panel, but the report is written),
  or a SteamVR (OpenVR) game, with SteamVR open.
- Nothing else: the program is already built and the .NET runtime is included. The XRPerfSensors
  window uses Microsoft's WebView2 engine, present in Windows 11 and up-to-date Windows 10
  (the installer warns if it's missing: https://developer.microsoft.com/microsoft-edge/webview2/).
- Optional: the PawnIO driver (https://pawnio.eu/), only for the processor's temperature and clock.
  Without it, they show "--"; everything else works. To install it:
  winget install namazso.PawnIO   (then restart XRPerfSensors)


INSTALLING OR UPDATING
----------------------
1. Extract the whole archive (right-click the .zip, "Extract All").
2. In the extracted folder, double-click Installer.bat.
   - Windows may show "Windows protected your PC" or "The publisher could not be verified":
     the program isn't signed. Click "More info" then "Run anyway" (or "Run").
   - Answer Yes to the administrator permission request: it is needed to declare the layer to
     OpenXR.
3. Choose whether XRPerfSensors (the small program that reads the sensors) starts with Windows.

The program is installed in C:\XRPerfOverlay (or in place of an existing installation).
When updating, your settings and reports are kept. The extracted folder can be deleted once the
installation is finished.


USAGE
-----
Start an OpenXR game: the panel appears in the headset.

  Ctrl + Alt + O                     show / hide the panel
  Ctrl + Alt + P                     change position
  Ctrl + Alt + R                     reset AVG / MIN / MAX
  Ctrl + Alt + Page Up / Page Down   enlarge / shrink
  Ctrl + Alt + Home / End            more opaque / more transparent
  Ctrl + Alt + F                     show / hide the captured window
  Ctrl + Alt + G                     place the captured window in front of you
  Ctrl + Alt + S                     manual report mode: start / stop recording

XRPerfSensors icon near the clock: double-click it (or right-click > Open XR Perf Overlay) to open
the program's window. A menu on the left leads to each page:
  - Dashboard: the panel as it appears in the headset, quick switches, the processor, graphics
    card and video memory live, the last session;
  - Panel, Appearance, Watch, Window, Desktop mirror (for OBS), Measurements: all the settings,
    applied immediately, even in game. The Panel page also has the "OpenXR games (layer)" switch:
    when off, the layer is no longer loaded into OpenXR games at all (applies the next time a
    game starts);
  - Reports: list of sessions, analysis and curves on the same page, comparison of two sessions.
    The "Simple" view explains each part in illustrated sentences (grid of 100 frames, bars,
    gauges), with tips; "Detailed" gives all the numbers; "Comparison" overlays two sessions
    (Ctrl + click to choose two);
  - Interface: language, window colors (like the panel, one of the six themes or your own three
    colors), text size, animations, full screen.
F11 switches the window to full screen, Esc leaves it. When closed, it uses nothing: the program
stays in the notification area.

Reports (Measurements page, "Write a report"): every session (default), on hotkey, or never.
On hotkey: Ctrl+Alt+S starts recording (red dot and "REC" at the top of the panel), Ctrl+Alt+S
again stops it and writes the report for that stretch only. Under 5 seconds: nothing is written.
Works in SteamVR games too.

Language: French or English for everything (window, icon menu, panel and watch in the headset,
reports). By default, the Windows language. Change it on the Interface page.

A GPU time with a leading "~" is an estimate (card load x frame duration).

Watch (Watch page): time, session time, controllers' battery and FPS, on the wrist or in front of
the eyes, in three styles. On the wrist, it only appears when you look at it (option). The panel
can also be attached to a hand (Panel page). These two options are set up when the game starts:
if you turn them on mid-game, restart the game.

SteamVR (OpenVR) games: the panel, the watch and the reports work there too, thanks to the small
XRPerfVR program started by XRPerfSensors. It joins SteamVR when SteamVR is open and never starts
it. Option "Also in SteamVR games (OpenVR)", Panel page.

Desktop window in the headset (Window page): a stream chat, Discord or any other open window,
shown read-only. Choose the window in the list (the preview shows what will be displayed,
cropping included), then check "Show the window in the headset". Placed in the room, it stays in
place when you turn your head: Ctrl+Alt+G places it in front of you. It can also follow a hand.
Don't minimize the captured window: Windows no longer draws it (it can stay behind others). On
Windows 10, a yellow border surrounds it during capture.


UNINSTALLING
------------
Run uninstall.bat in the installation folder (C:\XRPerfOverlay), then delete that folder.


TROUBLESHOOTING
---------------
- Log: %LOCALAPPDATA%\XRPerfOverlay\XRPerfOverlay.log (one line per game start, with the version
  number; the log is written in French).
- "No sensors" in the panel: XRPerfSensors isn't running
  (C:\XRPerfOverlay\sensors\XRPerfSensors.exe).
- Nothing in an OpenXR game and an "OpenXR layer is off" banner in XRPerfSensors: turn it back on
  (banner link, Panel page or tray icon menu), then restart the game.
- The XRPerfSensors window stays empty or says WebView2 is missing: install WebView2
  (https://developer.microsoft.com/microsoft-edge/webview2/). The rest of the program works without it.
- A game behaves oddly: run uninstall.bat to remove the layer, and look at the log.
- Games protected by anti-cheat may refuse OpenXR API layers.

To report a problem, give the version number (at the bottom of the XRPerfSensors window's menu,
or in the VERSION file of the installation folder), the game, the headset and the OpenXR runtime,
and attach the log.

Full documentation: README.en.md (in French: README.md). License: LICENSE.txt (MIT).
Third-party components: THIRD-PARTY.md.
