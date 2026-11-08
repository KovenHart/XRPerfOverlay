// Programme simulé, pour mettre l'interface au point dans un navigateur ordinaire (hors WebView2).
// Les images du panneau et les rapports viennent du vrai code (layer et SessionReport.cs), produits
// à l'avance dans mock_images.js et mock_reports.js. Ce dossier n'est pas livré dans le paquet.
"use strict";

(function () {
  const params = new URLSearchParams(location.search);
  const load = src => new Promise(ok => { const s = document.createElement("script"); s.src = src; s.onload = ok; document.body.appendChild(s); });

  const overlay = {
    Visible: true, Position: 5, Distance: 1.0, OffsetX: 0.34, OffsetY: 0.26, Width: 0.36, Opacity: 0.82, GlobalOpacity: 1.0, FaceViewer: true,
    Clock: true, HeadsetInfo: true, PanelSession: true, PanelBattery: false, PowerColumn: false, VideoSource: "final", Layout: Number(params.get("layout") || 0), Theme: Number(params.get("ptheme") || 0),
    CustomBackground: "#101820", CustomText: "#f0f3f8", CustomAccent: "#7fd1c7", HeadsetName: "Pimax Dream Air",
    Anchor: 0, HandPlacement: 2, GazeOnly: false, HandAlong: 0, HandLift: 0,
    WatchEnabled: true, WatchAnchor: 1, WatchPlacement: 0, WatchStyle: 0, WatchSession: true, WatchBattery: true, WatchFps: true, WatchGazeOnly: true,
    WatchSize: 0.06, WatchOpacity: 1, WatchAlong: 0, WatchLift: 0,
    WindowEnabled: false, WindowAnchor: 2, WindowPlacement: 2, WindowGazeOnly: false, WindowWidth: 0.4, WindowAlong: 0, WindowLift: 0, WindowDistance: 0.8,
    GpuTiming: false, ReportMode: params.get("reports") || "auto", ReportHardware: true, OpenVR: true, VideoCapture: params.get("video") !== "off",
  };
  const desktop = {
    DesktopPanel: false, TopMost: true, Scale: 100, Opacity: 100, Anchor: 0,
    CaptureFps: 5, CaptureOpacity: 100, CropLeft: 0, CropTop: 0, CropRight: 0, CropBottom: 0,
    UiTheme: params.get("theme") || "panel", UiBackground: "#12161c", UiText: "#eef2f7", UiAccent: "#f2a65a", UiScale: 100, UiAnimations: true, UiStartFullscreen: false,
    Language: params.get("lang") || "auto",
    VideoCodec: "hevc", VideoHeight: 1440, VideoFps: 60, VideoQuality: "high", VideoBitrate: 40, VideoEye: 0, VideoAspect: "16:9", VideoField: 75, VideoOverlay: "corner", VideoOverlayCorner: "topleft", VideoOverlaySize: 30, VideoStabilize: "light", VideoLevel: false,
    VideoGameAudio: true, VideoMic: params.get("video") === "rec", VideoRenderDevice: "", VideoCaptureDevice: "", VideoFolder: "C:\\Users\\Player\\Videos\\XR Perf Overlay", VideoHotkey: (3 << 16) | 86,
  };
  // Enregistrement vidéo simulé : ?video=rec (en cours), wait (en attente d'un jeu), error (dernière fin sur erreur), off.
  const video = { available: true, codecs: params.get("codecs") != null ? Number(params.get("codecs")) : 7, state: 0, seconds: 0, bytes: 0, frames: 0, repeated: 0, dropped: 0,
    audio: 0, file: "", game: "", encoder: "", reason: "", hotkeyOk: params.get("hotkey") !== "taken", folder: desktop.VideoFolder };
  if (params.get("video") === "rec") Object.assign(video, { state: 2, seconds: 754, bytes: 3.71e9, audio: 3, game: "GothicRemake-Win64-Shipping", encoder: "NVIDIA HEVC Encoder MFT" });
  if (params.get("video") === "wait") video.state = 1;
  if (params.get("video") === "error") video.reason = "Le jeu s'est arrêté : le fichier a été terminé.";
  if (params.get("video") === "last") video.file = desktop.VideoFolder + "\\GothicRemake-Win64-Shipping 2026-11-08 21-14-03.mp4";
  function tickVideo() {
    if (video.state === 1 && Math.random() < 0.3) Object.assign(video, { state: 2, game: "GothicRemake-Win64-Shipping", encoder: "NVIDIA HEVC Encoder MFT",
      audio: (desktop.VideoGameAudio ? 1 : 0) | (desktop.VideoMic ? 2 : 0) });
    if (video.state === 2) { video.seconds += 1; video.bytes += (desktop.VideoQuality === "custom" ? desktop.VideoBitrate : 24) * 125000 + 24000; video.frames += desktop.VideoFps; }
    if (video.state === 3) { video.state = 0; video.reason = ""; }
    reply(Object.assign({ type: "video" }, video));
  }
  let lang = desktop.Language === "en" ? "en" : "fr";
  let page = "dashboard";
  let fullscreen = false;
  let timer = 0;
  const layer = { state: params.get("layer") || "enabled", busy: false, elevated: false };
  // Ordre des layers (page Configuration) : ?xrorder=fg met OFXR Bridge au-dessus d'XR Perf Overlay.
  let xrUndo = null;
  const reply = msg => setTimeout(() => window.App.receive(JSON.parse(JSON.stringify(msg))), 0);

  const sensors = { cpuName: "AMD Ryzen 7 9800X3D 8-Core Processor", gpuName: "NVIDIA GeForce RTX 4090", cpuLoad: 31, cpuTemp: 61, cpuMHz: 5040, gpuLoad: 78, gpuTemp: 66, gpuMHz: 2805, cpuWatts: 71, gpuWatts: 388, vramUsed: 21800, vramTotal: 24564 };
  const walk = (v, lo, hi, step) => Math.max(lo, Math.min(hi, v + (Math.random() - 0.5) * step));
  function tickSensors() {
    sensors.cpuLoad = walk(sensors.cpuLoad, 12, 70, 8); sensors.cpuTemp = walk(sensors.cpuTemp, 50, 80, 2);
    sensors.gpuLoad = walk(sensors.gpuLoad, 40, 99, 9); sensors.gpuTemp = walk(sensors.gpuTemp, 55, 82, 1.5);
    sensors.vramUsed = walk(sensors.vramUsed, 18000, 24000, 300);
    reply({ type: "sensors", sensors: Object.assign({}, sensors) });
  }

  function panelImage() {
    const key = "panel_" + (overlay.Layout || 0) + "_" + Math.min(5, overlay.Theme === 6 ? 0 : overlay.Theme || 0);
    const img = window.MOCK_IMAGES[key];
    reply({ type: "panelImage", png: img ? img.png : null, live: false });
  }

  function images() {
    if (["dashboard", "panel", "appearance", "desktop"].includes(page)) panelImage();
    if (page === "watch") reply({ type: "watchImage", png: (window.MOCK_IMAGES["watch_" + (overlay.WatchStyle || 0)] || {}).png });
    if (page === "window") reply({ type: "windowImage", png: null, status: lang === "en" ? "Window « Chat - Twitch » found, capture ready." : "Fenêtre « Chat - Twitch » trouvée, capture prête." });
  }

  function reportsData() {
    const data = window.MOCK_REPORTS[lang];
    return data;
  }

  // Relevé de configuration, comme l'envoie ConfigProbe.cs. ?cfg=issues : une configuration avec des soucis.
  let configCache = null;
  function configData() {
    if (!configCache) configCache = buildConfig();
    configCache.collectedAt = new Date().toISOString().slice(0, 19);
    return configCache;
  }

  function buildConfig() {
    const issues = params.get("cfg") === "issues";
    const P = "C:\\Program Files\\";
    const layer = (name, manifest, extra) => Object.assign({ scope: "machine", manifest, enabled: true, exists: true, readable: true, name, description: null,
      library: manifest.replace(/\.json$/, ".dll"), libraryExists: true, disableEnvironment: null, disabledByEnvironment: false, enableEnvironment: null,
      enabledByEnvironment: null, ours: false, current: false }, extra || {});
    return {
      collectedAt: new Date().toISOString().slice(0, 19),
      activeRuntime: issues
        ? { path: "C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\steamxr_win64.json", exists: true, readable: true, name: "SteamVR", library: null, libraryExists: true }
        : { path: P + "Pimax\\Runtime\\pimax_openxr.json", exists: true, readable: true, name: "Pimax OpenXR", library: P + "Pimax\\Runtime\\pimax_openxr.dll", libraryExists: true },
      envRuntime: null, envLayers: null,
      availableRuntimes: [
        { path: P + "Pimax\\Runtime\\pimax_openxr.json", exists: true, readable: true, name: "Pimax OpenXR", library: null, libraryExists: true },
        { path: "C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\steamxr_win64.json", exists: true, readable: true, name: "SteamVR", library: null, libraryExists: true },
      ],
      layers: [
        layer("XR_APILAYER_NOVENDOR_perf_overlay", "C:\\XRPerfOverlay\\XR_APILAYER_NOVENDOR_perf_overlay.json",
          { description: "XR Perf Overlay - affichage FPS / CPU / GPU dans le casque", ours: true, current: true }),
        layer("XR_APILAYER_NOVENDOR_ofxr_bridge", "%USERPROFILE%\\Mods\\OFXR-Bridge\\XR_APILAYER_NOVENDOR_ofxr_bridge.json", { enabled: !issues ? true : true }),
        layer("XR_APILAYER_MBUCCHIA_toolkit", P + "OpenXR-Toolkit\\XR_APILAYER_MBUCCHIA_toolkit.json", issues ? {} : { enabled: false }),
        issues ? layer(null, P + "OpenKneeboard\\OpenKneeboard-OpenXR.json", { exists: false, readable: false, library: null, libraryExists: null }) : null,
        issues ? layer("XR_APILAYER_NOVENDOR_other_framegen", "D:\\Mods\\SmoothFrames\\XR_APILAYER_NOVENDOR_ofxr_test.json", { libraryExists: false }) : null,
        layer("XR_APILAYER_NOVENDOR_OBSMirror", "C:\\Users\\Player\\AppData\\Local\\OBSMirror\\XR_APILAYER_NOVENDOR_OBSMirror.json", { scope: "user", description: "OpenXR OBS Mirror" }),
      ].filter(Boolean).map((l, i) => Object.assign(l, { id: i })).sort((x, y) => params.get("xrorder") === "fg" ? (y.id === 1) - (x.id === 1) : 0)
        .map((l, i) => Object.assign(l, { id: i })),
      openVrRuntime: "C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR", openVrRuntimeExists: true,
      software: [{ name: "OpenXR-Toolkit", version: "1.3.2" }, { name: "Pimax Play", version: "2.0.3" }, { name: "SteamVR", version: "" }],
      cpu: "AMD Ryzen 7 9800X3D 8-Core Processor", threads: 16, ramGb: issues ? 16 : 32,
      gpus: issues
        ? [{ name: "AMD Radeon(TM) Graphics", driverVersion: "31.0.24027.1012", vramGb: 0.5 }, { name: "NVIDIA GeForce RTX 4060", driverVersion: "32.0.15.6614", vramGb: 8 }]
        : [{ name: "NVIDIA GeForce RTX 4090", driverVersion: "32.0.15.6614", vramGb: 24 }],
      sensorGpu: issues ? "NVIDIA GeForce RTX 4060" : "NVIDIA GeForce RTX 4090",
      windows: "Windows 11 Pro", windowsVersion: "24H2", windowsBuild: "26100.4351",
      hags: issues ? "off" : "on", gameMode: "default",
      powerPlanId: issues ? "a1841308-3541-4fab-bc81-f71556f20b4a" : "381b4222-f694-41f0-9685-ff5bb260df2e", powerPlanName: null,
      lastSession: { game: "GothicRemake-Win64-Shipping", runtime: "Pimax OpenXR 1.2.1", headset: "Pimax Dream Air", openVR: false, start: "2026-10-19T21:14:00" },
      errors: [],
    };
  }

  function keepUndo() { xrUndo = JSON.parse(JSON.stringify(configCache.layers)); }
  function sendConfig() {
    reply({ type: "xrUndo", available: !!xrUndo });
    reply({ type: "config", config: configData() });
  }

  window.Mock = {
    receive(msg) {
      switch (msg.type) {
        case "ready":
          Promise.all([load("dev/mock_images.js"), load("dev/mock_reports.js")]).then(() => {
            reply({ type: "init", version: "1.9.0", layerVersion: "1.9.0", lang, overlay, desktop, unavailable: null, captureAvailable: true, previewAvailable: true,
              pawnio: params.get("pawnio") !== "0", fullscreen, layer, reportsFolder: "C:\\XRPerfOverlay\\sessions", page: params.get("page") || "dashboard" });
            reply({ type: "status", game: params.get("game") || "none", sensors: "ok" });
            reply(reportsData().list);
            for (let i = 0; i < 40; i++) tickSensors();
            reply(Object.assign({ type: "video" }, video));
            clearInterval(timer);
            timer = setInterval(() => { tickSensors(); images(); tickVideo(); }, 1000);
          });
          break;
        case "page":
          page = msg.name;
          images();
          break;
        case "set":
          (msg.scope === "overlay" ? overlay : desktop)[msg.key] = msg.value;
          if (msg.scope === "desktop" && msg.key === "Language") {
            lang = msg.value === "en" ? "en" : "fr";
            reply({ type: "lang", lang });
            reply(reportsData().list);
          }
          images();
          break;
        case "listReports":
          reply(reportsData().list);
          break;
        case "openReport": {
          const d = reportsData().details;
          const a = d[msg.ids[0]];
          if (!a) break;
          if (msg.ids.length > 1 && d[msg.ids[1]]) reply({ type: "report", report: a.report, other: d[msg.ids[1]].report, chosen: true, setup: "", hardware: "" });
          else reply(a);
          break;
        }
        case "listWindows":
          reply({ type: "windows", windows: [
            { id: "1", title: "Chat - Twitch", process: "chrome.exe", selected: true },
            { id: "2", title: "#vr-mods - Discord", process: "Discord.exe" },
            { id: "3", title: "OBS 31.0.2 - Profil : VR", process: "obs64.exe" },
            { id: "4", title: "XR Perf Overlay", process: "XRPerfSensors.exe" },
          ] });
          break;
        case "setLayer":
          // Comme l'invite UAC : un instant d'attente, puis le registre change.
          layer.busy = true;
          reply({ type: "layer", layer });
          setTimeout(() => {
            layer.busy = false; layer.state = msg.on ? "enabled" : "disabled";
            if (configCache) configCache.layers.filter(x => x.ours && x.current).forEach(x => { x.enabled = !!msg.on; });
            reply({ type: "layer", layer });
          }, 900);
          break;
        case "fullscreen":
          fullscreen = msg.on;
          reply({ type: "fullscreen", on: fullscreen });
          break;
        case "resetDefaults":
          Object.assign(overlay, { Position: 5, Distance: 1, OffsetX: 0.34, OffsetY: 0.26, Width: 0.36, Opacity: 0.82, GlobalOpacity: 1, Layout: 0, Theme: 0 });
          reply({ type: "overlay", overlay });
          break;
        case "diagnose":
          setTimeout(() => sendConfig(), 350);
          break;
        case "setXrLayer": {
          const l = configCache && configCache.layers.find(x => x.id === msg.id);
          if (!l) break;
          if (!msg.on && !confirm("« " + msg.label + " » ne sera plus chargée dans aucun jeu OpenXR. Désactiver cette layer ?")) break;
          keepUndo();
          l.enabled = !!msg.on;
          reply({ type: "toast", text: msg.on ? "Layer réactivée : elle sera chargée au prochain lancement d'un jeu." : "Layer désactivée : elle ne sera plus chargée au prochain lancement d'un jeu." });
          setTimeout(() => sendConfig(), 300);
          break;
        }
        case "removeXrLayer": {
          if (!configCache || !confirm("Retirer l'inscription de « " + msg.label + " » ?")) break;
          configCache.layers = configCache.layers.filter(x => !(x.id === msg.id && x.manifest === msg.manifest));
          reply({ type: "toast", text: "Inscription retirée." });
          setTimeout(() => sendConfig(), 300);
          break;
        }
        case "moveXrLayer": {
          const list = configCache ? configCache.layers : [];
          const l = list.find(x => x.id === msg.id && x.manifest === msg.manifest);
          const same = l ? list.filter(x => x.scope === l.scope) : [];
          const j = same.indexOf(l) + msg.delta;
          if (!l || j < 0 || j >= same.length) { reply({ type: "toast", text: "La liste a changé entre-temps." }); sendConfig(); break; }
          keepUndo();
          const other = same[j], a = list.indexOf(l), b = list.indexOf(other);
          list[a] = other; list[b] = l;
          list.forEach((x, i) => { x.id = i; });
          setTimeout(() => sendConfig(), 250);
          break;
        }
        case "undoXrLayers":
          if (xrUndo) { configCache.layers = xrUndo; xrUndo = null; }
          setTimeout(() => sendConfig(), 250);
          break;
        case "copyText":
          (navigator.clipboard ? navigator.clipboard.writeText(msg.text) : Promise.reject()).then(
            () => reply({ type: "toast", text: lang === "en" ? "Diagnosis copied to the clipboard." : "Diagnostic copié dans le presse-papiers." }),
            () => { console.log(msg.text); reply({ type: "toast", text: "Copie impossible ici (texte dans la console)" }); });
          break;
        case "videoStart":
          if (!overlay.VideoCapture) break;
          if (!(video.codecs & ({ h264: 1, hevc: 2, av1: 4 }[desktop.VideoCodec] || 1))) { reply({ type: "toast", text: "Pas d'encodeur matériel pour ce format sur ce PC." }); break; }
          Object.assign(video, { state: 1, seconds: 0, bytes: 0, frames: 0, reason: "", file: "" });
          reply(Object.assign({ type: "video" }, video));
          break;
        case "videoStop":
          if (video.state === 2) video.file = desktop.VideoFolder + "\\" + video.game + " 2026-11-08 21-14-03.mp4";
          video.state = video.state === 2 ? 3 : 0;
          reply(Object.assign({ type: "video" }, video));
          break;
        case "videoDevices":
          setTimeout(() => reply({ type: "videoDevices",
            render: [{ id: "{0.0.0.00000000}.{a1}", name: "Haut-parleurs (Pimax Dream Air Audio)" }, { id: "{0.0.0.00000000}.{a2}", name: "Casque (Realtek USB Audio)" }],
            capture: [{ id: "{0.0.1.00000000}.{b1}", name: "Microphone (Pimax Dream Air Audio)" }, { id: "{0.0.1.00000000}.{b2}", name: "Microphone (Blue Yeti)" }] }), 150);
          break;
        case "videoPickFolder":
        case "videoOpenFolder":
          reply({ type: "toast", text: "Dossier (simulation)" });
          break;
        case "deleteReports":
          if (confirm(msg.question)) reply({ type: "toast", text: lang === "en" ? "Moved to the Recycle Bin" : "Envoyé à la corbeille" });
          break;
      }
    },
  };
})();
