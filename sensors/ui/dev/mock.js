// Programme simulé, pour mettre l'interface au point dans un navigateur ordinaire (hors WebView2).
// Les images du panneau et les rapports viennent du vrai code (layer et SessionReport.cs), produits
// à l'avance dans mock_images.js et mock_reports.js. Ce dossier n'est pas livré dans le paquet.
"use strict";

(function () {
  const params = new URLSearchParams(location.search);
  const load = src => new Promise(ok => { const s = document.createElement("script"); s.src = src; s.onload = ok; document.body.appendChild(s); });

  const overlay = {
    Visible: true, Position: 5, Distance: 1.0, OffsetX: 0.34, OffsetY: 0.26, Width: 0.36, Opacity: 0.82, GlobalOpacity: 1.0, FaceViewer: true,
    Clock: true, HeadsetInfo: true, PanelSession: true, PanelBattery: false, Layout: Number(params.get("layout") || 0), Theme: Number(params.get("ptheme") || 0),
    CustomBackground: "#101820", CustomText: "#f0f3f8", CustomAccent: "#7fd1c7", HeadsetName: "Pimax Dream Air",
    Anchor: 0, HandPlacement: 2, GazeOnly: false, HandAlong: 0, HandLift: 0,
    WatchEnabled: true, WatchAnchor: 1, WatchPlacement: 0, WatchStyle: 0, WatchSession: true, WatchBattery: true, WatchFps: true, WatchGazeOnly: true,
    WatchSize: 0.06, WatchOpacity: 1, WatchAlong: 0, WatchLift: 0,
    WindowEnabled: false, WindowAnchor: 2, WindowPlacement: 2, WindowGazeOnly: false, WindowWidth: 0.4, WindowAlong: 0, WindowLift: 0, WindowDistance: 0.8,
    GpuTiming: false, ReportMode: params.get("reports") || "auto", ReportHardware: true, OpenVR: true,
  };
  const desktop = {
    DesktopPanel: false, TopMost: true, Scale: 100, Opacity: 100, Anchor: 0,
    CaptureFps: 5, CaptureOpacity: 100, CropLeft: 0, CropTop: 0, CropRight: 0, CropBottom: 0,
    UiTheme: params.get("theme") || "panel", UiBackground: "#12161c", UiText: "#eef2f7", UiAccent: "#f2a65a", UiScale: 100, UiAnimations: true, UiStartFullscreen: false,
    Language: params.get("lang") || "auto",
  };
  let lang = desktop.Language === "en" ? "en" : "fr";
  let page = "dashboard";
  let fullscreen = false;
  let timer = 0;
  const layer = { state: params.get("layer") || "enabled", busy: false, elevated: false };
  const reply = msg => setTimeout(() => window.App.receive(JSON.parse(JSON.stringify(msg))), 0);

  const sensors = { cpuName: "AMD Ryzen 7 9800X3D 8-Core Processor", gpuName: "NVIDIA GeForce RTX 4090", cpuLoad: 31, cpuTemp: 61, cpuMHz: 5040, gpuLoad: 78, gpuTemp: 66, gpuMHz: 2805, vramUsed: 21800, vramTotal: 24564 };
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

  window.Mock = {
    receive(msg) {
      switch (msg.type) {
        case "ready":
          Promise.all([load("dev/mock_images.js"), load("dev/mock_reports.js")]).then(() => {
            reply({ type: "init", version: "1.5.0", layerVersion: "1.5.0", lang, overlay, desktop, unavailable: null, captureAvailable: true, previewAvailable: true,
              pawnio: params.get("pawnio") !== "0", fullscreen, layer, reportsFolder: "C:\\XRPerfOverlay\\sessions", page: params.get("page") || "dashboard" });
            reply({ type: "status", game: params.get("game") || "none", sensors: "ok" });
            reply(reportsData().list);
            for (let i = 0; i < 40; i++) tickSensors();
            clearInterval(timer);
            timer = setInterval(() => { tickSensors(); images(); }, 1000);
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
          setTimeout(() => { layer.busy = false; layer.state = msg.on ? "enabled" : "disabled"; reply({ type: "layer", layer }); }, 900);
          break;
        case "fullscreen":
          fullscreen = msg.on;
          reply({ type: "fullscreen", on: fullscreen });
          break;
        case "resetDefaults":
          Object.assign(overlay, { Position: 5, Distance: 1, OffsetX: 0.34, OffsetY: 0.26, Width: 0.36, Opacity: 0.82, GlobalOpacity: 1, Layout: 0, Theme: 0 });
          reply({ type: "overlay", overlay });
          break;
        case "deleteReports":
          if (confirm(msg.question)) reply({ type: "toast", text: lang === "en" ? "Moved to the Recycle Bin" : "Envoyé à la corbeille" });
          break;
      }
    },
  };
})();
