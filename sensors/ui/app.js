// Interface de XRPerfSensors : navigation, réglages, tableau de bord et rapports.
// Échange avec le programme par messages JSON (WebView2 : chrome.webview.postMessage / "message").
// Sans WebView2 (ouverture dans un navigateur pour la mise au point), dev/mock.js simule le programme.
"use strict";

window.App = (function () {
  const host = window.chrome && window.chrome.webview ? window.chrome.webview : null;

  const state = {
    ready: false,
    lang: "fr",
    version: "",
    layerVersion: "",
    overlay: {},
    desktop: {},
    defaults: { overlay: {}, desktop: {} },  // valeurs par défaut : réglages modifiés, « Rétablir »
    tab: { panel: "panel", diagnostics: "config", video: "rec" },  // onglet ouvert de chaque page à onglets
    unavailable: null,
    captureAvailable: true,
    previewAvailable: true,
    pawnio: true,
    fullscreen: false,
    reportsFolder: "",
    sensorsState: "wait",
    game: "none",
    sensors: null,
    history: { cpuLoad: [], cpuTemp: [], gpuLoad: [], gpuTemp: [], vram: [] },
    page: "dashboard",
    panelImage: null,
    panelLive: false,
    watchImage: null,
    windowImage: null,
    captureStatus: "",
    video: null,          // état de l'enregistrement vidéo (message "video")
    videoDevices: null,   // sorties et entrées audio
    windows: [],
    reports: [],
    reportFolders: [],
    gameFilter: "",
    search: "",
    selected: [],
    report: null,
    other: null,
    otherChosen: false,
    compareNote: { setup: "", hardware: "" },
    readMode: "simple",
    reportView: "analysis",
    config: undefined,     // relevé de configuration : undefined = pas encore demandé, null = échec
    configLoading: false,
  };

  // --- Outils ----------------------------------------------------------------------------------

  function t(key, params) {
    const table = window.I18N[state.lang] || window.I18N.fr;
    let s = table[key];
    if (s == null) s = window.I18N.fr[key];
    if (s == null) return key;
    if (params) for (const k in params) s = s.split("{" + k + "}").join(params[k]);
    return s;
  }

  function num(v, digits) {
    if (v == null || isNaN(v)) return "--";
    return new Intl.NumberFormat(state.lang === "en" ? "en-US" : "fr-FR", { minimumFractionDigits: digits || 0, maximumFractionDigits: digits || 0 }).format(v);
  }

  function h(tag, props) {
    const el = document.createElement(tag);
    if (props) {
      for (const k in props) {
        const v = props[k];
        if (v == null || v === false) continue;
        if (k === "class") el.className = v;
        else if (k === "text") el.textContent = v;
        else if (k === "style") Object.assign(el.style, v);
        else if (k.startsWith("on")) el.addEventListener(k.slice(2).toLowerCase(), v);
        else if (v === true) el.setAttribute(k, "");
        else el.setAttribute(k, v);
      }
    }
    for (let i = 2; i < arguments.length; i++) append(el, arguments[i]);
    return el;
  }

  function append(el, child) {
    if (child == null || child === false) return;
    if (Array.isArray(child)) { child.forEach(c => append(el, c)); return; }
    el.appendChild(typeof child === "string" || typeof child === "number" ? document.createTextNode(String(child)) : child);
  }

  const ICONS = {
    dashboard: '<path d="M4 13h6V4H4zM14 20h6V11h-6zM4 20h6v-3H4zM14 7h6V4h-6z"/>',
    up: '<path d="m6 15 6-6 6 6"/>',
    down: '<path d="m6 9 6 6 6-6"/>',
    panel: '<rect x="3" y="5" width="18" height="12" rx="2.5"/><path d="M7 9.5h4M7 13h7M15 9.5h2"/>',
    appearance: '<path d="M12 3a9 9 0 1 0 0 18c1.2 0 1.6-.8 1.6-1.6 0-1.3-1-1.5-1-2.6 0-1 .8-1.8 1.8-1.8H17a4 4 0 0 0 4-4C21 6.6 17 3 12 3z"/><circle cx="7.5" cy="11" r="1.2"/><circle cx="10.5" cy="7.2" r="1.2"/><circle cx="15" cy="7.6" r="1.2"/>',
    watch: '<circle cx="12" cy="12" r="6"/><path d="M12 9.5V12l1.6 1.2M9 6.5 9.6 3h4.8l.6 3.5M9 17.5l.6 3.5h4.8l.6-3.5"/>',
    window: '<rect x="3" y="4" width="18" height="16" rx="2.5"/><path d="M3 8.5h18M6.5 6.3h.01M9 6.3h.01"/><path d="M8 13h8M8 16h5"/>',
    mirror: '<rect x="2.5" y="4" width="19" height="13" rx="2"/><path d="M9 21h6M12 17v4"/><rect x="6" y="7.5" width="7" height="4.5" rx="1"/>',
    hotkeys: '<rect x="2.5" y="6" width="19" height="12" rx="2.5"/><path d="M6.5 10h1M10 10h1M13.5 10h1M17 10h.5M6.5 14h1M10 14h4.5M17 14h.5"/>',
    image: '<rect x="3" y="4.5" width="18" height="15" rx="2.5"/><circle cx="9" cy="10" r="1.8"/><path d="m3.5 17.5 5-4.5 3.5 3 3-2.5 5.5 4.5"/>',
    video: '<rect x="2.5" y="6" width="13" height="12" rx="2.5"/><path d="m15.5 10.2 6-3.2v10l-6-3.2"/>',
    measures: '<path d="M4 18a8 8 0 1 1 16 0"/><path d="M12 18l4-6"/><path d="M4 18h2M18 18h2M12 10V8"/>',
    reports: '<path d="M7 3h7l5 5v13H7z"/><path d="M14 3v5h5"/><path d="M10 17v-3M13 17v-5M16 17v-2"/>',
    interface: '<path d="M4 7h10M18 7h2M4 17h4M12 17h8"/><circle cx="16" cy="7" r="2"/><circle cx="10" cy="17" r="2"/>',
    expand: '<path d="M4 9V4h5M20 9V4h-5M4 15v5h5M20 15v5h-5"/>',
    compress: '<path d="M9 4v5H4M15 4v5h5M9 20v-5H4M15 20v-5h5"/>',
    refresh: '<path d="M20 11a8 8 0 1 0-2.3 5.7"/><path d="M20 4v7h-7"/>',
    trash: '<path d="M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3"/>',
    folder: '<path d="M3 6.5A1.5 1.5 0 0 1 4.5 5H9l2 2.5h8.5A1.5 1.5 0 0 1 21 9v9.5a1.5 1.5 0 0 1-1.5 1.5h-15A1.5 1.5 0 0 1 3 18.5z"/>',
    search: '<circle cx="11" cy="11" r="6.5"/><path d="M20 20l-4.2-4.2"/>',
    close: '<path d="M6 6l12 12M18 6L6 18"/>',
    camera: '<path d="M4 8.5A1.5 1.5 0 0 1 5.5 7h2.2l1.5-2h5.6l1.5 2h2.2A1.5 1.5 0 0 1 20 8.5v9a1.5 1.5 0 0 1-1.5 1.5h-13A1.5 1.5 0 0 1 4 17.5z"/><circle cx="12" cy="13" r="3.5"/>',
    chevron: '<path d="M9 6l6 6-6 6"/>',
    text: '<path d="M7 3h7l5 5v13H7z"/><path d="M14 3v5h5M10 12h6M10 15h6M10 18h4"/>',
    alert: '<path d="M12 4 2.5 20h19z"/><path d="M12 10v4.5M12 17.5h.01"/>',
    good: '<circle cx="12" cy="12" r="9"/><path d="m8 12.5 2.7 2.7L16.5 9"/>',
    info: '<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7.5h.01"/>',
    problem: '<circle cx="12" cy="12" r="9"/><path d="m9 9 6 6M15 9l-6 6"/>',
    tip: '<path d="M9 18h6M10 21h4M12 3a6 6 0 0 0-3.6 10.8c.6.5 1.1 1.3 1.1 2.2h5c0-.9.5-1.7 1.1-2.2A6 6 0 0 0 12 3z"/>',
    compare: '<path d="M8 4v16M16 4v16M4 8h4M16 16h4M4 14h4M16 10h4"/>',
    config: '<rect x="6" y="6" width="12" height="12" rx="2"/><rect x="9.5" y="9.5" width="5" height="5" rx=".8"/><path d="M9 3v3M15 3v3M9 18v3M15 18v3M3 9h3M3 15h3M18 9h3M18 15h3"/>',
    copy: '<rect x="8" y="8" width="12" height="12" rx="2"/><path d="M16 8V5.5A1.5 1.5 0 0 0 14.5 4h-9A1.5 1.5 0 0 0 4 5.5v9A1.5 1.5 0 0 0 5.5 16H8"/>',
  };

  function icon(name) {
    const span = document.createElement("span");
    span.innerHTML = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">' + (ICONS[name] || "") + "</svg>";
    return span.firstChild;
  }

  // --- Échanges avec le programme -----------------------------------------------------------------

  let indexing = false;  // recherche : les pages sont construites hors écran, sans rien envoyer
  function send(type, payload) {
    if (indexing) return;
    const msg = Object.assign({ type }, payload || {});
    if (host) host.postMessage(msg);
    else if (window.Mock) window.Mock.receive(msg);
  }

  function receive(msg) {
    if (!msg || !msg.type) return;
    switch (msg.type) {
      case "init":
        Object.assign(state, {
          version: msg.version, layerVersion: msg.layerVersion || "", overlay: msg.overlay || {}, desktop: msg.desktop || {},
          unavailable: msg.unavailable || null, captureAvailable: msg.captureAvailable !== false, previewAvailable: msg.previewAvailable !== false,
          pawnio: msg.pawnio !== false, fullscreen: !!msg.fullscreen, reportsFolder: msg.reportsFolder || "",
          layer: msg.layer || { state: "enabled", busy: false, elevated: false },
        });
        state.defaults = msg.defaults || { overlay: {}, desktop: {} };
        state.lang = msg.lang === "en" ? "en" : "fr";
        state.readMode = msg.readMode === "detailed" ? "detailed" : "simple";
        state.ready = true;
        document.getElementById("app").hidden = false;
        applyTheme();
        state.page = homePage();
        renderAll();
        go(msg.page || homePage());
        if (simpleMode() && !get("desktop", "UiWelcome")) openWelcome();
        break;
      case "overlay":
      case "desktop": {
        // Réglages changés ailleurs (raccourcis en jeu, menu de l'icône) : seules les pages qui les montrent sont redessinées.
        const key = msg.type;
        const fresh = msg[key] || state[key];
        const changed = JSON.stringify(fresh) !== JSON.stringify(state[key]);
        const modeChanged = key === "desktop" && fresh.UiMode !== state.desktop.UiMode;
        state[key] = fresh;
        if (!changed) break;
        applyTheme();
        if (modeChanged) { go(homePage()); break; }
        renderTopbar();
        if (!isDragging() && state.page !== "reports") renderPage();
        break;
      }
      case "lang":
        // Les textes venus du programme (analyses, état de la capture) sont redemandés dans la nouvelle langue.
        state.lang = msg.lang === "en" ? "en" : "fr";
        renderAll();
        send("page", { name: state.page });
        send("listReports");
        if (state.selected.length) send("openReport", { ids: state.selected });
        break;
      case "fullscreen":
        state.fullscreen = !!msg.on;
        renderTopbar();
        if (state.page === "interface") renderPage();
        break;
      case "video": {
        const before = state.video;
        state.video = msg;
        // Le bouton et l'état changent sur place ; la page n'est redessinée que si les codecs viennent d'être connus.
        if (state.page === "video" && before && before.codecs !== msg.codecs) renderPage();
        else updateVideo();
        if (!before || before.state !== msg.state || msg.state !== 0) renderTopbar();
        break;
      }
      case "videoDevices":
        state.videoDevices = { render: msg.render || [], capture: msg.capture || [] };
        if (state.page === "video") fillDevices();
        break;
      case "status":
        if (msg.game) state.game = msg.game;
        if (msg.sensors) state.sensorsState = msg.sensors;
        if (msg.capture != null) state.captureStatus = msg.capture;
        renderTopbar();
        renderRailFoot();
        updateLive();
        break;
      case "sensors":
        state.sensors = msg.sensors;
        pushHistory(msg.sensors);
        state.sensorsState = "ok";
        updateLive();
        break;
      case "panelImage":
        state.panelImage = msg.png ? "data:image/png;base64," + msg.png : null;
        state.panelLive = !!msg.live;
        updateLive();
        break;
      case "watchImage":
        state.watchImage = msg.png ? "data:image/png;base64," + msg.png : null;
        updateLive();
        break;
      case "windowImage":
        state.windowImage = msg.png ? "data:image/png;base64," + msg.png : null;
        if (msg.status != null) state.captureStatus = msg.status;
        updateLive();
        break;
      case "windows":
        state.windows = msg.windows || [];
        if (state.page === "window") renderWindowList();
        break;
      case "reports":
        state.reports = msg.items || [];
        state.reportFolders = msg.folders || [];
        // Sélection gardée si les rapports existent encore ; sinon le plus récent.
        state.selected = state.selected.filter(id => state.reports.some(r => r.id === id));
        if (state.page === "reports") {
          if (state.selected.length === 0 && visibleReports().length) selectReports([visibleReports()[0].id]);
          else renderReportList();
        }
        if (state.page === "dashboard" || state.page === "home") renderPage();
        break;
      case "report":
        state.report = msg.report || null;
        state.other = msg.other || null;
        state.otherChosen = !!msg.chosen;
        state.compareNote = { setup: msg.setup || "", hardware: msg.hardware || "" };
        if (state.page === "reports") renderReportView(true);
        break;
      case "toast":
        toast(msg.text);
        break;
      case "xrUndo":
        // Envoyé juste avant le relevé ("config") ; seul, il redessine la page si le bouton apparaît ou disparaît.
        if (state.xrUndo !== !!msg.available) {
          state.xrUndo = !!msg.available;
          if (currentSub() === "config" && state.config !== undefined && !state.configLoading) renderPage();
        }
        break;
      case "layer":
        // Layer activée ou désactivée (ici, depuis le menu de l'icône ou par l'installateur).
        if (!msg.layer || JSON.stringify(msg.layer) === JSON.stringify(state.layer)) break;
        state.layer = msg.layer;
        if (["dashboard", "home", "panel", "simple-headset"].includes(state.page) && !isDragging()) renderPage();
        if (currentSub() === "config") requestConfig();
        break;
      case "config":
        state.config = msg.config || null;
        state.configLoading = false;
        state.xrBusy = false;
        if (currentSub() === "config") renderPage();
        break;
      case "go":
        go(msg.page);
        break;
    }
  }

  if (host) host.addEventListener("message", e => receive(e.data));

  function pushHistory(s) {
    const add = (k, v) => { const a = state.history[k]; a.push(v == null ? null : v); if (a.length > 120) a.shift(); };
    add("cpuLoad", s.cpuLoad); add("cpuTemp", s.cpuTemp); add("gpuLoad", s.gpuLoad); add("gpuTemp", s.gpuTemp);
    add("vram", s.vramUsed != null ? s.vramUsed / 1024 : null);
  }

  let dragging = false;
  document.addEventListener("pointerdown", e => { if (e.target && e.target.type === "range") dragging = true; });
  document.addEventListener("pointerup", () => { dragging = false; });
  const isDragging = () => dragging;

  // --- Thème de l'interface ------------------------------------------------------------------------

  // Palettes du panneau (layer/src/panel.cpp) : fond, texte, intitulés, accent, traits, bon, moyen, mauvais.
  const PANEL_THEMES = [
    ["#0e1116", "#f0f3f8", "#8e98aa", "#96bef0", "#464e5e", "#6ee18c", "#ffbe46", "#ff645a"],
    ["#f6f7f9", "#161a22", "#606a7a", "#1c62c8", "#c4cad4", "#168c42", "#ba6c00", "#cc2a2a"],
    ["#000000", "#ffffff", "#cdcdcd", "#ffdd00", "#787878", "#00ff80", "#ffaa00", "#ff5050"],
    ["#050e08", "#aaffbe", "#54a068", "#3ce66e", "#205430", "#3ce66e", "#ffc83c", "#ff6050"],
    ["#140d05", "#ffdeaa", "#b0824a", "#ffb000", "#60421a", "#96dc6e", "#ff9628", "#ff5a46"],
    ["#110c1c", "#f2eefc", "#9a90b4", "#c48cff", "#4a3c66", "#5aebbe", "#ffc45a", "#ff6082"],
    ["#0e1116", "#f0f3f8", "#8e98aa", "#96bef0", "#464e5e", "#6ee18c", "#ffbe46", "#ff645a"],  // 6 : personnalisé (voir customPalette)
    ["#1e1e1e", "#f3f3f3", "#9c9c9c", "#ff8c1f", "#3b3b3b", "#35c135", "#ffd23f", "#ff5a4f"],  // 7 : Classic
    ["#18181e", "#ececf2", "#8f8f9d", "#6d7cff", "#3b3b48", "#2fbf63", "#ffb547", "#ff5d63"],  // 8 : EVO
    ["#0c0716", "#f4eeff", "#a596cc", "#00e5f5", "#342456", "#3dffa2", "#ffd23d", "#ff3d6e"],  // 9 : Néon
    ["#0c0c0a", "#f2f0e6", "#a8a594", "#f5e626", "#3c3c30", "#2ee6c5", "#ff9a2e", "#ff3b3b"],  // 10 : Signal
  ];
  // Panneau en Classic ou EVO : avec « comme le panneau », la fenêtre prend aussi l'allure de ce thème.
  const PANEL_STYLES = { 7: "classic", 8: "evo", 9: "neon", 10: "signal" };
  const PANEL_THEME_ORDER = [0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 6];
  const UI_THEMES = ["panel", "dark", "light", "contrast", "green", "amber", "violet", "classic", "evo", "neon", "signal", "custom"];

  // Thèmes de la fenêtre seulement, avec leur propre allure (formes, typographie) en plus des couleurs :
  // « Classic » (gris, accent vert, titres arrondis) et « EVO » (bleu nuit, accent indigo, capitales,
  // angles vifs). Couleurs relevées sur les captures de référence.
  const STYLED_THEMES = {
    classic: {
      palette: ["#121212", "#f3f3f3", "#9c9c9c", "#35c135", "#2a2a2a", "#35c135", "#ffb02e", "#ff5a4f"],
      vars: { "--canvas": "#121212", "--rail": "#1a1a1a", "--surface": "#1e1e1e", "--raised": "#262626", "--hover": "#2b2b2b",
        "--line": "#2c2c2c", "--line-strong": "#3b3b3b", "--text": "#f3f3f3", "--text-2": "#c9c9c9", "--muted": "#9c9c9c",
        "--accent": "#35c135", "--accent-ink": "#ffffff", "--title": "#ff8c1f" },
    },
    evo: {
      palette: ["#111115", "#ececf2", "#8f8f9d", "#6d7cff", "#2a2a34", "#2fbf63", "#ffb547", "#ff5d63"],
      vars: { "--canvas": "#111115", "--rail": "#141419", "--surface": "#18181e", "--raised": "#1e1e26", "--hover": "#22222c",
        "--line": "#2a2a34", "--line-strong": "#3b3b48", "--text": "#ececf2", "--text-2": "#c4c4ce", "--muted": "#8f8f9d",
        "--accent": "#6d7cff", "--accent-ink": "#ffffff", "--accent-strong": "#4b5ae0", "--title": "#ececf2" },
    },
    // Néon : violet nuit, accent magenta lumineux, cyan pour les liens et repères (thème Cyberpunk).
    neon: {
      palette: ["#0a0612", "#f4eeff", "#a596cc", "#ff2bd6", "#2e1f4d", "#3dffa2", "#ffd23d", "#ff3d6e"],
      vars: { "--canvas": "#0a0612", "--rail": "#0d0818", "--surface": "#120b20", "--raised": "#1a1030", "--hover": "#20143a",
        "--line": "#2e1f4d", "--line-strong": "#4a3577", "--text": "#f4eeff", "--text-2": "#c9bfe6", "--muted": "#a596cc",
        "--accent": "#ff2bd6", "--accent-ink": "#0a0612", "--accent-strong": "#ff2bd6", "--title": "#f4eeff" },
    },
    // Signal : noir et jaune, coins coupés, sans halo (thème Cyberpunk).
    signal: {
      palette: ["#0b0b09", "#f2f0e6", "#a8a594", "#f5e626", "#33332a", "#2ee6c5", "#ff9a2e", "#ff3b3b"],
      vars: { "--canvas": "#0b0b09", "--rail": "#0f0f0c", "--surface": "#141410", "--raised": "#1d1d17", "--hover": "#232319",
        "--line": "#33332a", "--line-strong": "#4a4a3c", "--text": "#f2f0e6", "--text-2": "#d6d3c4", "--muted": "#a8a594",
        "--accent": "#f5e626", "--accent-ink": "#0b0b09", "--accent-strong": "#f5e626", "--title": "#f2f0e6" },
    },
  };

  function rgb(hex) {
    const m = /^#?([0-9a-f]{6})$/i.exec(hex || "");
    const v = m ? parseInt(m[1], 16) : 0;
    return [(v >> 16) & 255, (v >> 8) & 255, v & 255];
  }
  const toHex = c => "#" + c.map(x => Math.max(0, Math.min(255, Math.round(x))).toString(16).padStart(2, "0")).join("");
  const mix = (a, b, t) => toHex(rgb(a).map((x, i) => x + (rgb(b)[i] - x) * t));
  const lum = hex => { const c = rgb(hex); return (c[0] * 299 + c[1] * 587 + c[2] * 114) / 1000; };

  function customPalette(bg, text, accent) {
    const light = lum(bg) > 140;
    const ref = PANEL_THEMES[light ? 1 : 0];
    return [bg, text, mix(text, bg, 105 / 255), accent, mix(bg, text, 66 / 255), ref[5], ref[6], ref[7]];
  }

  function palette() {
    const d = state.desktop, o = state.overlay;
    let theme = d.UiTheme || "panel";
    if (theme === "panel") {
      const id = o.Theme != null ? o.Theme : 0;
      if (id === 6) return customPalette(o.CustomBackground || "#0e1116", o.CustomText || "#f0f3f8", o.CustomAccent || "#96bef0");
      if (PANEL_STYLES[id]) return STYLED_THEMES[PANEL_STYLES[id]].palette;
      return PANEL_THEMES[id] || PANEL_THEMES[0];
    }
    if (theme === "custom") return customPalette(d.UiBackground || "#0e1116", d.UiText || "#f0f3f8", d.UiAccent || "#96bef0");
    if (STYLED_THEMES[theme]) return STYLED_THEMES[theme].palette;
    const index = UI_THEMES.indexOf(theme) - 1;
    return PANEL_THEMES[index] || PANEL_THEMES[0];
  }

  // Allure de la fenêtre : Classic ou EVO, choisi pour la fenêtre ou repris du panneau.
  function styleName() {
    const u = state.desktop.UiTheme || "panel";
    if (STYLED_THEMES[u]) return u;
    return u === "panel" ? PANEL_STYLES[state.overlay.Theme] || null : null;
  }

  function applyTheme() {
    const [bg, text, muted, accent, , good, warn, bad] = palette();
    const light = lum(bg) > 140;
    const root = document.documentElement.style;
    const set = (k, v) => root.setProperty(k, v);
    set("--canvas", light ? mix(bg, "#000000", 0.035) : bg);
    set("--surface", light ? bg : mix(bg, text, 0.035));
    set("--raised", mix(bg, text, light ? 0.06 : 0.07));
    set("--hover", mix(bg, text, light ? 0.09 : 0.1));
    set("--line", mix(bg, text, light ? 0.12 : 0.13));
    set("--line-strong", mix(bg, text, light ? 0.22 : 0.22));
    set("--text", text);
    set("--text-2", mix(text, bg, 0.2));
    set("--muted", muted);
    set("--accent", accent);
    set("--accent-ink", lum(accent) > 140 ? mix(bg, "#000000", light ? 0.8 : 0.3) : "#ffffff");
    const wash = (c, a) => { const v = rgb(c); return `rgba(${v[0]},${v[1]},${v[2]},${a})`; };
    set("--accent-wash", wash(accent, light ? 0.12 : 0.14));
    set("--good", good); set("--warn", warn); set("--bad", bad);
    set("--good-wash", wash(good, light ? 0.13 : 0.13));
    set("--warn-wash", wash(warn, light ? 0.16 : 0.13));
    set("--bad-wash", wash(bad, light ? 0.12 : 0.13));
    // Séries des courbes : mêmes teintes partout, un pas plus soutenu sur fond clair.
    set("--s-fps", light ? "#2a78d6" : "#3987e5");
    set("--s-fps-2", light ? "#6250d6" : "#9085e9");
    set("--s-cpu", light ? "#eb6834" : "#d95926");
    set("--s-gpu", light ? "#1baf7a" : "#199e70");
    set("--s-vram", light ? "#2a78d6" : "#3987e5");
    // Classic et EVO : surfaces exactes et allure propre (attribut data-style, voir app.css).
    const style = styleName();
    const styled = style ? STYLED_THEMES[style] : null;
    for (const k of ["--rail", "--title", "--accent-strong"]) root.removeProperty(k);
    if (styled) {
      for (const [k, v] of Object.entries(styled.vars)) set(k, v);
      set("--accent-wash", wash(styled.vars["--accent"], 0.14));
      document.documentElement.dataset.style = style;
    } else {
      delete document.documentElement.dataset.style;
    }
    document.documentElement.style.colorScheme = light ? "light" : "dark";
    const scale = Math.max(80, Math.min(140, state.desktop.UiScale || 100)) / 100;
    set("--ui-scale", String(scale));
    document.documentElement.classList.toggle("no-anim", state.desktop.UiAnimations === false);
    document.documentElement.lang = state.lang;
    send("chrome", { background: bg, light });
  }

  const cssVar = name => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

  // --- Navigation -----------------------------------------------------------------------------------

  // Deux interfaces : simple (cinq pages, choix tout faits, pour commencer) et avancée (tout, rangé par
  // tâche). Les pages avancées restent ouvertes depuis le mode simple (liens « Tous les réglages »).
  const SIMPLE_PAGES = [
    { id: "home", icon: "dashboard" },
    { id: "simple-headset", icon: "panel" },
    { id: "simple-video", icon: "video" },
    { id: "simple-image", icon: "image" },
    { id: "reports", icon: "reports" },
  ];
  const ADVANCED_PAGES = [
    { id: "dashboard", icon: "dashboard" },
    { group: "nav.group.ingame" },
    { id: "panel", icon: "panel" },
    { id: "watch", icon: "watch" },
    { id: "window", icon: "window" },
    { group: "nav.group.image" },
    { id: "image", icon: "image" },
    { group: "nav.group.capture" },
    { id: "video", icon: "video" },
    { id: "desktop", icon: "mirror" },
    { group: "nav.group.analysis" },
    { id: "reports", icon: "reports" },
    { id: "diagnostics", icon: "config" },
  ];
  const BOTTOM_PAGES = [{ id: "hotkeys", icon: "hotkeys" }, { id: "interface", icon: "interface" }];
  // Anciennes pages devenues des onglets (liens du programme, du menu de l'icône).
  const ALIASES = { appearance: ["panel", "appearance"], measures: ["diagnostics", "measures"], config: ["diagnostics", "config"] };
  const TABS = {
    panel: [{ id: "panel", label: "tab.panel.place" }, { id: "appearance", label: "tab.panel.look" }],
    diagnostics: [{ id: "config", label: "tab.diag.config" }, { id: "measures", label: "tab.diag.measures" }],
    video: ["rec", "live", "snap", "sound", "camera", "obs"].map(id => ({ id, label: "tab.video." + id })),
  };
  const ALL_PAGES = SIMPLE_PAGES.concat(ADVANCED_PAGES, BOTTOM_PAGES).filter(p => p.id);

  const simpleMode = () => get("desktop", "UiMode") === "simple";
  const homePage = () => (simpleMode() ? "home" : "dashboard");

  // Nom de la page « au sens du programme » (aperçus à envoyer, relevé de configuration) : l'onglet ouvert.
  function currentSub() {
    if (state.page === "panel") return state.tab.panel === "appearance" ? "appearance" : "panel";
    if (state.page === "diagnostics") return state.tab.diagnostics === "measures" ? "measures" : "config";
    return state.page;
  }

  function go(page, tab) {
    stopHotkeyCapture();
    closeSearch();
    if (ALIASES[page]) [page, tab] = ALIASES[page];
    if (page === "dashboard" && simpleMode()) page = "home";
    if (page === "home" && !simpleMode()) page = "dashboard";
    if (!ALL_PAGES.some(p => p.id === page)) page = homePage();
    if (tab && TABS[page] && TABS[page].some(x => x.id === tab)) state.tab[page] = tab;
    state.page = page;
    send("page", { name: currentSub() });
    renderRail();
    renderTopbar();
    renderPage();
    const main = document.getElementById("page");
    main.scrollTop = 0;
    if (page === "reports") send("listReports");
    if (page === "window") send("listWindows");
    if (currentSub() === "config") requestConfig();
  }

  // Barre d'onglets en haut d'une page ; `before` : contenu commun à tous les onglets (sous la barre).
  function tabBar(pageId) {
    const bar = h("div", { class: "tabs", role: "tablist", "aria-label": t("nav." + pageId) });
    for (const x of TABS[pageId]) {
      const selected = state.tab[pageId] === x.id;
      bar.appendChild(h("button", { type: "button", role: "tab", "aria-selected": String(selected), class: "tab", text: t(x.label),
        onclick: () => { if (!selected) go(pageId, x.id); } }));
    }
    return bar;
  }

  function renderAll() {
    document.title = "XR Perf Studio";
    renderRail();
    renderTopbar();
    renderBanner();
    renderPage();
  }

  function brandMark() {
    const s = document.createElement("span");
    s.innerHTML = '<svg class="brand-mark" viewBox="0 0 32 32" aria-hidden="true"><path d="M4 11.5C4 9 6 7 8.5 7h15C26 7 28 9 28 11.5v7c0 2.5-2 4.5-4.5 4.5h-4l-2-3.2a1.8 1.8 0 0 0-3 0l-2 3.2h-4C6 23 4 21 4 18.5z" fill="var(--accent)"/><path d="M9 17.5v-3M12.5 17.5v-6M16 17.5v-4.5M19.5 17.5v-7.5M23 17.5v-2" stroke="var(--canvas)" stroke-width="2" stroke-linecap="round"/></svg>';
    return s.firstChild;
  }

  function navItem(p) {
    const item = h("button", { class: "nav-item", type: "button", title: t("nav." + p.id), "aria-current": state.page === p.id ? "page" : null, onclick: () => go(p.id) },
      icon(p.icon), h("span", { text: t("nav." + p.id) }));
    if ((p.id === "dashboard" || p.id === "home") && (!state.pawnio || versionMismatch())) item.appendChild(h("i", { class: "nav-badge", "aria-hidden": "true" }));
    return item;
  }

  function renderRail() {
    const rail = document.getElementById("rail");
    rail.replaceChildren();
    rail.appendChild(h("div", { class: "brand" }, brandMark(), h("div", { class: "brand-name" }, "XR Perf Studio", h("span", { text: t("app.sub") }))));
    rail.appendChild(h("button", { class: "nav-search", type: "button", onclick: openSearch, "aria-keyshortcuts": "Control+K" },
      icon("search"), h("span", { text: t("search.open") }), h("kbd", { text: "Ctrl K" })));
    for (const p of simpleMode() ? SIMPLE_PAGES : ADVANCED_PAGES) {
      if (p.group) { rail.appendChild(h("div", { class: "nav-group", text: t(p.group) })); continue; }
      rail.appendChild(navItem(p));
    }
    const bottom = h("div", { class: "rail-bottom" });
    if (!simpleMode()) BOTTOM_PAGES.forEach(p => bottom.appendChild(navItem(p)));
    const mode = h("input", { type: "checkbox", role: "switch", "aria-label": t("mode.advanced") });
    mode.checked = !simpleMode();
    mode.addEventListener("change", () => { setValue("desktop", "UiMode", mode.checked ? "advanced" : "simple"); go(homePage()); });
    bottom.appendChild(h("label", { class: "mode-switch", title: t("mode.advanced.help") }, h("span", { text: t("mode.advanced") }), h("span", { class: "switch" }, mode, h("span"))));
    rail.appendChild(bottom);
    rail.appendChild(h("div", { class: "rail-foot", id: "rail-foot" }));
    renderRailFoot();
  }

  function renderRailFoot() {
    const foot = document.getElementById("rail-foot");
    if (!foot) return;
    const s = state.sensorsState;
    const cls = s === "ok" ? "pill live" : s === "error" ? "pill bad" : "pill paused";
    foot.replaceChildren(
      h("div", { class: "sensor-line" }, h("span", { class: cls }, h("i", { class: "dot" }), t("foot.sensors." + (s === "ok" ? "ok" : s === "error" ? "error" : "wait")))),
      h("div", { text: t("foot.live") }),
      h("div", { text: "XRPerfSensors " + state.version }));
  }

  function renderTopbar() {
    const bar = document.getElementById("topbar");
    if (!bar) return;
    const g = state.game;
    const pill = h("span", { class: "pill " + (g === "live" ? "live" : g === "paused" ? "paused" : ""), id: "game-pill" }, h("i", { class: "dot" }), t("game." + (g === "live" ? "live" : g === "paused" ? "paused" : "none")));
    const fs = h("button", { class: "btn icon ghost", type: "button", title: t(state.fullscreen ? "top.exitFullscreen" : "top.fullscreen"), "aria-label": t(state.fullscreen ? "top.exitFullscreen" : "top.fullscreen"), onclick: toggleFullscreen },
      icon(state.fullscreen ? "compress" : "expand"));
    // Enregistrement vidéo en cours (ou en attente d'un jeu) : témoin sur toutes les pages, qui mène à la page Vidéo.
    const v = state.video;
    const rec = v && (v.state === 1 || v.state === 2 || v.state === 3)
      ? h("button", { class: "pill rec" + (v.state === 2 ? " on" : ""), type: "button", title: t("nav.video"), onclick: () => go("video") }, h("i", { class: "dot" }),
          v.state === 2 ? (v.streaming ? "LIVE " : "REC ") + clock(v.seconds) : t(v.state === 1 ? "vid.pill.waiting" : "vid.pill.finishing"))
      : null;
    // Preset d'image en cours (filtres actifs) : rappel discret, mène à la page Image.
    const preset = currentPreset();
    const presetChip = preset && (get("overlay", "ImageMode") || "off") !== "off"
      ? h("button", { class: "pill", type: "button", title: t("nav.image"), onclick: () => go(simpleMode() ? "simple-image" : "image") }, icon("image"), presetLabel(preset))
      : null;
    bar.replaceChildren(h("h1", { text: t("nav." + state.page) }), h("div", { class: "actions" }, rec, presetChip, pill, fs));
  }

  function toggleFullscreen() {
    send("fullscreen", { on: !state.fullscreen });
  }

  function versionMismatch() {
    return state.layerVersion && state.version && state.version !== "dev" && state.layerVersion !== state.version;
  }

  function renderBanner() {
    const b = document.getElementById("banner");
    b.replaceChildren();
    b.className = "banner";
    if (state.unavailable) {
      b.classList.add("bad");
      b.append(icon("alert"), h("div", null, h("b", { text: t("banner.unavailable") }), h("div", { class: "small", text: state.unavailable })));
      b.hidden = false;
      return;
    }
    b.hidden = true;
  }

  function renderPage() {
    const page = document.getElementById("page");
    if (!page || !state.ready) return;
    Charts.hideTip();
    page.className = "page";
    page.replaceChildren();
    const builder = PAGE_BUILDERS[state.page];
    if (builder) builder(page);
    updateLive();
  }

  // --- Contrôles de réglage -------------------------------------------------------------------------

  // Chaque réglage est relié à un champ de state.overlay (casque, .ini de la layer) ou de state.desktop
  // (XRPerfSensors.ini). Le changement est envoyé tout de suite ; le programme l'applique et l'enregistre.
  let refreshers = [];

  function setValue(scope, key, value) {
    state[scope][key] = value;
    send("set", { scope, key, value });
    if (scope === "overlay" && (key === "Theme" || key.startsWith("Custom"))) applyTheme();
    if (scope === "desktop" && key.startsWith("Ui")) applyTheme();
    refreshers.forEach(f => f());
  }

  const get = (scope, key) => state[scope][key];
  const locked = scope => scope === "overlay" && !!state.unavailable;

  // Réglage à sa valeur par défaut ? (valeurs envoyées par le programme ; inconnu : oui)
  function isDefault(scope, key) {
    const d = state.defaults[scope] || {};
    if (!(key in d)) return true;
    const a = get(scope, key), b = d[key];
    if (typeof a === "number" && typeof b === "number") return Math.abs(a - b) < 1e-4;
    return JSON.stringify(a) === JSON.stringify(b);
  }

  // Mode avancé : pastille sur un réglage modifié ; la ligne garde son réglage pour « Rétablir » (group).
  function trackDefault(r, scope, key) {
    r.dataset.scope = scope;
    r.dataset.key = key;
    if (simpleMode()) return r;
    const label = r.querySelector(".row-label");
    if (!label || !(key in (state.defaults[scope] || {}))) return r;
    const dot = h("i", { class: "mod-dot", title: t("set.modified"), role: "img", "aria-label": t("set.modified") });
    label.appendChild(dot);
    const f = () => { dot.hidden = isDefault(scope, key); };
    refreshers.push(f);
    f();
    return r;
  }

  function row(label, help, control, opts) {
    opts = opts || {};
    const r = h("div", { class: "row" + (opts.stack ? " stack" : "") },
      h("div", null, h("div", { class: "row-label", text: label }), help ? h("div", { class: "row-help", text: help }) : null),
      h("div", { class: "row-control" }, control));
    if (opts.enabled) {
      const f = () => r.classList.toggle("disabled", !opts.enabled());
      refreshers.push(f);
      f();
    }
    return r;
  }

  function switchCtl(scope, key, label, help, opts) {
    const input = h("input", { type: "checkbox", role: "switch", "aria-label": label });
    input.checked = !!get(scope, key);
    input.disabled = locked(scope);
    input.addEventListener("change", () => setValue(scope, key, input.checked));
    return trackDefault(row(label, help, h("label", { class: "switch" }, input, h("span")), opts), scope, key);
  }

  // Interrupteur de la layer OpenXR : l'état vient du registre (message "layer"), pas des réglages.
  function layerSwitch(label, withHelp) {
    const L = state.layer || {};
    const input = h("input", { type: "checkbox", role: "switch", "aria-label": label });
    input.checked = L.state === "enabled";
    input.disabled = L.state === "missing" || !!L.busy;
    input.addEventListener("change", () => {
      const on = input.checked;
      input.checked = !on;  // l'état réel arrive avec la réponse (invite Windows acceptée ou non)
      input.disabled = true;
      send("setLayer", { on });
    });
    let help = null;
    if (L.state === "missing") help = t("layer.missing");
    else if (L.busy) help = t("layer.busy");
    else if (withHelp) help = t("layer.help") + (L.elevated ? "" : " " + t("layer.help.admin"));
    return row(label, help, h("label", { class: "switch" }, input, h("span")));
  }

  function segmented(options, current, onPick, label) {
    const seg = h("div", { class: "seg", role: "group", "aria-label": label || null });
    const buttons = options.map(o => {
      const b = h("button", { type: "button", "aria-pressed": String(o.value === current), text: o.label, onclick: () => {
        buttons.forEach(x => x.setAttribute("aria-pressed", "false"));
        b.setAttribute("aria-pressed", "true");
        onPick(o.value);
      } });
      return b;
    });
    buttons.forEach(b => seg.appendChild(b));
    return seg;
  }

  function segRow(scope, key, label, help, options, opts) {
    const seg = segmented(options, get(scope, key), v => setValue(scope, key, v), label);
    if (locked(scope)) seg.querySelectorAll("button").forEach(b => (b.disabled = true));
    return trackDefault(row(label, help, seg, opts), scope, key);
  }

  function selectRow(scope, key, label, help, options, opts) {
    const sel = h("select", { class: "select", "aria-label": label });
    options.forEach(o => sel.appendChild(h("option", { value: String(o.value), text: o.label })));
    sel.value = String(get(scope, key));
    sel.disabled = locked(scope);
    sel.addEventListener("change", () => setValue(scope, key, typeof options[0].value === "number" ? Number(sel.value) : sel.value));
    return trackDefault(row(label, help, sel, opts), scope, key);
  }

  /** Curseur : la valeur enregistrée vaut position / factor (par exemple des mètres pour des centimètres affichés). */
  function sliderRow(scope, key, label, help, min, max, format, opts) {
    opts = opts || {};
    const factor = opts.factor || 1;
    const input = h("input", { type: "range", min, max, step: opts.step || 1, "aria-label": label });
    const out = h("output");
    const show = () => {
      const v = Number(input.value);
      out.textContent = format(v);
      input.style.setProperty("--fill", ((v - min) / (max - min)) * 100 + "%");
    };
    const raw = get(scope, key);
    input.value = String(Math.round((raw == null ? min : raw) * factor));
    input.disabled = locked(scope);
    input.addEventListener("input", () => { show(); setValue(scope, key, Number(input.value) / factor); });
    show();
    return trackDefault(row(label, help, h("div", { class: "slider" }, input, out), opts), scope, key);
  }

  const cm = v => num(v) + " " + t("unit.cm");
  const signedCm = v => (v > 0 ? "+" : "") + num(v) + " " + t("unit.cm");
  const pct = v => num(v) + " " + t("unit.pct");
  const meters = v => num(v / 100, 2) + " " + t("unit.m");

  function group(title, help, children, enabled) {
    const head = h("div", { class: "group-head" }, h("h2", { text: title }), help ? h("p", { text: help }) : null);
    const g = h("section", { class: "group", "data-off": t("set.offWhen") }, head, children);
    if (enabled) {
      const f = () => g.setAttribute("aria-disabled", String(!enabled()));
      refreshers.push(f);
      f();
    }
    // Mode avancé : « Rétablir » remet les réglages modifiés de la section à leur valeur par défaut.
    const tracked = Array.from(g.querySelectorAll(".row[data-key]")).map(r => [r.dataset.scope, r.dataset.key])
      .filter(([scope, key]) => key in (state.defaults[scope] || {}));
    if (tracked.length && !simpleMode()) {
      const reset = h("button", { class: "btn small ghost group-reset", type: "button", text: t("set.resetSection"), onclick: () => {
        for (const [scope, key] of tracked) if (!isDefault(scope, key) && !locked(scope)) setValue(scope, key, state.defaults[scope][key]);
        renderPage();
        toast(t("set.resetSection.done"));
      } });
      head.appendChild(reset);
      const f = () => { reset.hidden = tracked.every(([scope, key]) => isDefault(scope, key)); };
      refreshers.push(f);
      f();
    }
    return g;
  }

  function colorPick(scope, key, label) {
    const input = h("input", { type: "color", "aria-label": label });
    const swatch = h("i");
    const value = get(scope, key) || "#000000";
    input.value = value;
    swatch.style.background = value;
    input.disabled = locked(scope);
    input.addEventListener("input", () => { swatch.style.background = input.value; setValue(scope, key, input.value); });
    return h("label", { class: "color-pick" }, swatch, h("span", { text: label }), input);
  }

  function settingsLayout(page, form, stage) {
    page.appendChild(h("div", { class: "settings" + (stage ? "" : " single") }, h("div", { class: "settings-form" }, form), stage ? h("div", { class: "stage-col" }, stage) : null));
  }

  function panelStage(caption) {
    return [
      h("div", { class: "stage", id: "panel-stage" }, h("img", { id: "panel-img", alt: t("dash.preview") }), h("div", { class: "stage-empty", id: "panel-empty", hidden: true, text: t("dash.preview.none") })),
      h("div", { class: "stage-caption" }, h("span", { id: "panel-caption", text: caption || t("set.preview.sample") })),
    ];
  }

  const anchorOptions = () => [0, 1, 2].map(v => ({ value: v, label: t(["anchor.view", "anchor.left", "anchor.right"][v]) }));
  const placementOptions = () => [0, 1, 2].map(v => ({ value: v, label: t("place." + v) }));

  // --- Tableau de bord --------------------------------------------------------------------------------

  // Avertissements du tableau de bord et de l'accueil (layer coupée, versions, PawnIO).
  function dashNotes(dash) {
    const layerOff = state.layer && state.layer.state === "disabled";
    if (!state.pawnio || versionMismatch() || layerOff) {
      const notes = [];
      if (layerOff) notes.push(h("div", { class: "banner" }, icon("alert"), h("div", null, t("banner.layerOff") + " ",
        h("a", { href: "#", onclick: e => { e.preventDefault(); if (!state.layer.busy) send("setLayer", { on: true }); }, text: t("banner.layerOff.action") }))));
      if (versionMismatch()) notes.push(h("div", { class: "banner" }, icon("alert"), h("div", { text: t("banner.mismatch", { layer: state.layerVersion, app: state.version }) })));
      if (!state.pawnio) notes.push(h("div", { class: "banner" }, icon("alert"), h("div", null, t("banner.pawnio") + " ",
        h("a", { href: "#", onclick: e => { e.preventDefault(); send("openUrl", { url: "https://pawnio.eu/" }); }, text: t("banner.pawnio.action") }))));
      notes.forEach(n => { n.style.margin = "0"; });
      dash.appendChild(h("div", { class: "span", style: { display: "grid", gap: "8px" } }, notes));
    }
  }

  function renderDashboard(page) {
    refreshers = [];
    const dash = h("div", { class: "dash" });
    dashNotes(dash);

    // Le panneau tel qu'il est dans le casque.
    const stageCard = h("section", { class: "card dash-stage" },
      h("div", { class: "card-head" }, h("h2", { text: t("dash.preview") }), h("span", { class: "muted small", id: "panel-caption" })),
      h("div", { class: "stage", id: "panel-stage" }, h("img", { id: "panel-img", alt: t("dash.preview") }), h("div", { class: "stage-empty", id: "panel-empty", hidden: true, text: t("dash.preview.none") })));
    dash.appendChild(stageCard);

    const quick = h("section", { class: "card" }, h("div", { class: "card-head" }, h("h2", { text: t("dash.quick") })),
      h("div", { class: "quick" },
        layerSwitch(t("dash.quick.layer"), false),
        switchCtl("overlay", "Visible", t("dash.quick.panel"), hk("toggle").key),
        switchCtl("overlay", "WatchEnabled", t("dash.quick.watch")),
        switchCtl("overlay", "WindowEnabled", t("dash.quick.window"), hk("window_toggle").key),
        switchCtl("overlay", "OpenVR", t("dash.quick.openvr")),
        switchCtl("desktop", "DesktopPanel", t("dash.quick.mirror"))));
    dash.appendChild(quick);

    // Capteurs en direct.
    const tiles = h("div", { class: "tiles" },
      h("div", { class: "tile", id: "tile-cpu" }),
      h("div", { class: "tile", id: "tile-gpu" }),
      h("div", { class: "tile", id: "tile-vram" }));
    dash.appendChild(h("section", { class: "card span" }, h("div", { class: "card-head" }, h("h2", { text: t("dash.sensors") })), tiles));

    dash.appendChild(lastSessionCard());

    // Raccourcis : l'aide-mémoire complet s'ouvre à la demande (touche « ? »).
    dash.appendChild(h("section", { class: "card" }, h("div", { class: "card-head" }, h("h2", { text: t("dash.keys") })),
      h("p", { class: "muted", style: { margin: "0 0 14px" }, text: t("dash.keys.short", { panel: hotkeyText(get("overlay", "Hotkey.toggle") || 0) || "--",
        video: hotkeyText(get("desktop", "VideoHotkey") || 0) || "--" }) }),
      h("button", { class: "btn", type: "button", onclick: openCheatSheet }, icon("hotkeys"), t("dash.keys.all"), h("kbd", { text: "?" }))));
    page.appendChild(dash);
  }

  // Dernière session : anneau de cible tenue, verdict, lien vers le rapport.
  function lastSessionCard() {
    const last = state.reports.find(r => r.readable);
    const lastCard = h("section", { class: "card" }, h("div", { class: "card-head" }, h("h2", { text: t("dash.last") })));
    if (!last) lastCard.appendChild(h("p", { class: "muted", text: t("dash.last.none") }));
    else {
      const lvl = levelClass(last.level);
      lastCard.appendChild(h("div", { class: "last" },
        Charts.ring(last.targetHeld, lvl ? `var(--${lvl})` : "var(--accent)", last.targetHeld == null ? "--" : num(last.targetHeld) + " %", t("dash.last.held")),
        h("div", null,
          h("h3", { style: { fontSize: "1.35rem" }, text: last.game }),
          h("div", { class: "muted", text: formatDate(last.start, true) + (last.duration ? " · " + last.duration : "") }),
          h("div", { style: { marginTop: "8px" } }, h("span", { class: "pill " + (lvl || ""), }, h("i", { class: "dot" }), last.verdict || "")),
          h("div", { class: "tile-sub", style: { marginTop: "10px" } }, h("span", null, h("b", { text: num(last.fpsAvg, 1) }), " " + t("dash.last.fps")))),
        h("button", { class: "btn", type: "button", onclick: () => { state.selected = [last.id]; go("reports"); send("openReport", { ids: [last.id] }); } }, t("dash.last.open"))));
    }
    return lastCard;
  }

  // Aide-mémoire : raccourcis du moment (page Raccourcis) ; ceux retirés n'apparaissent pas.
  function openCheatSheet() {
    const k = combo => h("span", { class: "combo" }, hotkeyParts(combo).map(x => h("kbd", { text: x })));
    const keyList = HOTKEYS.filter(x => get("overlay", "Hotkey." + x.name))
      .map(x => [k(get("overlay", "Hotkey." + x.name)), h("span", { text: t(x.label) })]);
    openModal(t("dash.keys"), h("div", null,
      h("div", { class: "keys" }, keyList,
        videoEnabled() && get("desktop", "VideoHotkey") ? [k(get("desktop", "VideoHotkey")), h("span", { text: t("key.video") })] : null,
        videoEnabled() && get("desktop", "SnapHotkey") ? [k(get("desktop", "SnapHotkey")), h("span", { text: t("key.snap") })] : null),
      h("p", { class: "muted small", style: { marginTop: "16px" } }, t("dash.keys.change") + " ",
        h("a", { href: "#", onclick: e => { e.preventDefault(); closeModal(); go("hotkeys"); }, text: t("nav.hotkeys") }))));
  }

  function tile(id, label, name, value, unit, subs, spark, color, min, max, meter) {
    const el = document.getElementById(id);
    if (!el) return;
    el.replaceChildren(
      h("div", { class: "tile-label" }, h("span", { text: label }), name ? h("span", { class: "name", text: name, title: name }) : null),
      h("div", { class: "tile-value" }, value, h("small", { text: unit })),
      h("div", { class: "tile-sub" }, subs.map(s => h("span", null, h("b", { text: s[0] }), " " + s[1]))),
      meter != null ? h("div", { class: "meter" }, h("i", { style: { width: Math.max(0, Math.min(100, meter)) + "%" } })) : Charts.spark(spark, color, min, max));
  }

  // Mise à jour des parties vivantes sans reconstruire la page (images, capteurs, état de la capture).
  function updateLive() {
    const img = document.getElementById("panel-img");
    if (img) {
      const empty = document.getElementById("panel-empty");
      if (state.panelImage) { if (img.getAttribute("src") !== state.panelImage) img.src = state.panelImage; img.hidden = false; if (empty) empty.hidden = true; }
      else { img.hidden = true; if (empty) empty.hidden = state.previewAvailable; if (empty && !state.previewAvailable) empty.hidden = false; }
      const cap = document.getElementById("panel-caption");
      if (cap && (state.page === "dashboard" || state.page === "home")) cap.textContent = state.panelLive ? t("dash.preview.live") : t("dash.preview.sample");
    }
    const watch = document.getElementById("watch-img");
    if (watch && state.watchImage) watch.src = state.watchImage;
    const win = document.getElementById("window-img");
    if (win) {
      if (state.windowImage) { win.src = state.windowImage; win.hidden = false; document.getElementById("window-empty").hidden = true; }
      else { win.hidden = true; document.getElementById("window-empty").hidden = false; }
    }
    const status = document.getElementById("window-status");
    if (status) {
      let text = state.captureStatus || "";
      if (!state.captureAvailable) text = t("win.unavailable");
      else if (!text && !state.overlay.WindowEnabled) text = t("win.disabledNote");
      status.textContent = text;
    }
    if (state.page === "dashboard" || state.page === "home") {
      const s = state.sensors || {};
      const hist = state.history;
      tile("tile-cpu", t("dash.cpu"), s.cpuName, num(s.cpuLoad), " %", [[num(s.cpuTemp) + " °C", t("dash.temp")], [num(s.cpuMHz), "MHz"]].concat(s.cpuWatts != null ? [[num(s.cpuWatts), "W"]] : []), hist.cpuLoad, "var(--s-cpu)", 0, 100);
      tile("tile-gpu", t("dash.gpu"), s.gpuName, num(s.gpuLoad), " %", [[num(s.gpuTemp) + " °C", t("dash.temp")], [num(s.gpuMHz), "MHz"]].concat(s.gpuWatts != null ? [[num(s.gpuWatts), "W"]] : []), hist.gpuLoad, "var(--s-gpu)", 0, 100);
      const used = s.vramUsed != null ? s.vramUsed / 1024 : null, total = s.vramTotal != null ? s.vramTotal / 1024 : null;
      tile("tile-vram", t("dash.vram"), null, num(used, 1), " " + t("unit.gb"), [[t("dash.of", { total: num(total, 1) }), ""]], null, null, null, null,
        used != null && total ? (100 * used) / total : 0);
    }
  }

  // --- Panneau ------------------------------------------------------------------------------------

  function spotPicker() {
    const spots = [[18, 24], [50, 24], [82, 24], [82, 76], [50, 76], [18, 76]];
    const box = h("div", { class: "spot-picker", role: "group", "aria-label": t("panel.position") }, h("i", { class: "eye" }));
    const buttons = spots.map((p, i) => {
      const b = h("button", { type: "button", title: t("pos." + i), "aria-label": t("pos." + i), "aria-pressed": String(get("overlay", "Position") === i), style: { left: p[0] + "%", top: p[1] + "%" },
        onclick: () => { buttons.forEach(x => x.setAttribute("aria-pressed", "false")); b.setAttribute("aria-pressed", "true"); setValue("overlay", "Position", i); } });
      b.disabled = locked("overlay");
      box.appendChild(b);
      return b;
    });
    return box;
  }

  function renderPanelPage(page) {
    refreshers = [];
    const onView = () => get("overlay", "Anchor") === 0;
    const form = [
      group(t("panel.show"), null, [
        switchCtl("overlay", "Visible", t("panel.visible"), t("panel.visible.help", hk("toggle"))),
        segRow("overlay", "Anchor", t("panel.anchor"), null, anchorOptions()),
      ]),
      group(t("panel.view"), t("panel.view.help"), [
        row(t("panel.position"), t("panel.position.help", hk("position")), spotPicker()),
        sliderRow("overlay", "Distance", t("panel.distance"), null, 30, 500, meters, { factor: 100 }),
        sliderRow("overlay", "OffsetX", t("panel.offsetX"), null, 0, 300, cm, { factor: 100 }),
        sliderRow("overlay", "OffsetY", t("panel.offsetY"), null, 0, 300, cm, { factor: 100 }),
        switchCtl("overlay", "FaceViewer", t("panel.face")),
      ], onView),
      group(t("panel.hand"), t("panel.hand.help"), [
        selectRow("overlay", "HandPlacement", t("panel.placement"), null, placementOptions()),
        switchCtl("overlay", "GazeOnly", t("panel.gaze")),
        sliderRow("overlay", "HandAlong", t("panel.along"), null, -12, 12, signedCm, { factor: 100 }),
        sliderRow("overlay", "HandLift", t("panel.lift"), null, -5, 10, signedCm, { factor: 100 }),
      ], () => !onView()),
      group(t("panel.size"), null, [
        sliderRow("overlay", "Width", t("panel.width"), t("panel.width.help", hk("size_up", "size_down")), 10, 150, cm, { factor: 100 }),
        sliderRow("overlay", "GlobalOpacity", t("panel.globalOpacity"), t("panel.globalOpacity.help", hk("opacity_up", "opacity_down")), 20, 100, pct, { factor: 100 }),
        sliderRow("overlay", "Opacity", t("panel.opacity"), null, 0, 100, pct, { factor: 100 }),
      ]),
      group(t("panel.games"), null, [
        layerSwitch(t("layer.label"), true),
        switchCtl("overlay", "OpenVR", t("panel.openvr.label"), t("panel.openvr.help")),
      ]),
      defaultsButton(),
    ];
    settingsLayout(page, form, panelStage());
  }

  function defaultsButton() {
    const b = h("button", { class: "btn", type: "button", onclick: () => send("resetDefaults") }, t("set.defaults"));
    b.disabled = !!state.unavailable;
    return h("div", { style: { display: "flex", gap: "14px", alignItems: "center" } }, b, h("span", { class: "muted small", text: t("set.defaults.help") }));
  }

  // --- Apparence ---------------------------------------------------------------------------------

  function themeChips(scope, key, values, labelOf, paletteOf) {
    const box = h("div", { class: "themes", role: "group" });
    const chips = values.map(v => {
      const p = paletteOf(v);
      const sample = h("span", { class: "sample", style: { background: p[0], color: p[1] } }, h("b", { text: "72" }), h("i", { style: { background: p[3] } }));
      const chip = h("button", { type: "button", class: "theme-chip", "aria-pressed": String(get(scope, key) === v),
        onclick: () => { chips.forEach(c => c.setAttribute("aria-pressed", "false")); chip.setAttribute("aria-pressed", "true"); setValue(scope, key, v); } },
        sample, h("span", { class: "name", text: labelOf(v) }));
      chip.disabled = locked(scope);
      box.appendChild(chip);
      return chip;
    });
    return box;
  }

  // Nom du casque affiché (panneau, rapports) à la place de celui que le runtime annonce. Enregistré quand
  // on quitte le champ ou appuie sur Entrée ; vaut au prochain lancement d'un jeu.
  function headsetNameRow() {
    const input = h("input", { class: "text-input", type: "text", maxlength: "40", spellcheck: "false", autocomplete: "off",
      placeholder: t("app.headsetName.auto"), "aria-label": t("app.headsetName") });
    input.value = get("overlay", "HeadsetName") || "";
    input.disabled = locked("overlay");
    const help = t("app.headsetName.help");
    const r = row(t("app.headsetName"), help, input);
    const helpEl = r.querySelector(".row-help");
    const clean = v => v.replace(/[^\x20-\x7E]/g, "").replace(/[;\[\]]/g, "").trim().slice(0, 40);
    const commit = () => {
      const v = clean(input.value);
      input.value = v;
      helpEl.textContent = help;
      if (v === (get("overlay", "HeadsetName") || "")) return;
      setValue("overlay", "HeadsetName", v);
      toast(t("app.headsetName.saved"));
    };
    input.addEventListener("input", () => {
      const bad = /[^\x20-\x7E]|[;\[\]]/.test(input.value);
      helpEl.textContent = bad ? t("app.headsetName.ascii") : help;
    });
    input.addEventListener("change", commit);
    input.addEventListener("keydown", e => { if (e.key === "Enter") input.blur(); });
    return r;
  }

  function renderAppearancePage(page) {
    refreshers = [];
    const o = state.overlay;
    const layoutRow = segRow("overlay", "Layout", t("app.layout"), null, [0, 1, 2].map(v => ({ value: v, label: t("layout." + v) })));
    const layoutHelp = h("div", { class: "row-help" });
    const showHelp = () => { layoutHelp.textContent = t("layout." + (get("overlay", "Layout") || 0) + ".help"); };
    refreshers.push(showHelp);
    showHelp();
    layoutRow.firstChild.appendChild(layoutHelp);
    const custom = h("div", { class: "colors" }, colorPick("overlay", "CustomBackground", t("color.bg")), colorPick("overlay", "CustomText", t("color.text")), colorPick("overlay", "CustomAccent", t("color.accent")));
    const customRow = row(t("theme.6"), null, custom, { enabled: () => get("overlay", "Theme") === 6 });
    const form = [
      group(t("app.layout"), null, [layoutRow]),
      group(t("app.colors"), t("app.colors.help"), [
        row("", null, themeChips("overlay", "Theme", PANEL_THEME_ORDER, v => t("theme." + v),
          v => v === 6 ? customPalette(o.CustomBackground || "#0e1116", o.CustomText || "#f0f3f8", o.CustomAccent || "#96bef0") : PANEL_THEMES[v]), { stack: true }),
        customRow,
      ]),
      group(t("app.elements"), null, [
        switchCtl("overlay", "Clock", t("app.clock")),
        switchCtl("overlay", "HeadsetInfo", t("app.headset")),
        headsetNameRow(),
        switchCtl("overlay", "PanelSession", t("app.session")),
        switchCtl("overlay", "PanelBattery", t("app.battery"), t("app.battery.help")),
        switchCtl("overlay", "PowerColumn", t("app.power"), t("app.power.help")),
      ]),
      defaultsButton(),
    ];
    settingsLayout(page, form, panelStage());
  }

  // --- Montre ------------------------------------------------------------------------------------

  function renderWatchPage(page) {
    refreshers = [];
    const on = () => !!get("overlay", "WatchEnabled");
    const onHand = () => on() && get("overlay", "WatchAnchor") !== 0;
    const elements = h("div", { class: "seg", role: "group" });
    [["WatchSession", "watch.session"], ["WatchBattery", "watch.battery"], ["WatchFps", "watch.fps"]].forEach(([key, label]) => {
      const b = h("button", { type: "button", "aria-pressed": String(!!get("overlay", key)), text: t(label), onclick: () => {
        const v = !get("overlay", key);
        b.setAttribute("aria-pressed", String(v));
        setValue("overlay", key, v);
      } });
      b.disabled = locked("overlay");
      elements.appendChild(b);
    });
    const form = [
      group(t("nav.watch"), null, [switchCtl("overlay", "WatchEnabled", t("watch.show"), t("watch.show.help"))]),
      group(t("watch.where"), null, [
        segRow("overlay", "WatchAnchor", t("watch.anchor"), null, anchorOptions()),
        selectRow("overlay", "WatchPlacement", t("panel.placement"), null, placementOptions(), { enabled: onHand }),
        switchCtl("overlay", "WatchGazeOnly", t("watch.gaze"), null, { enabled: onHand }),
        sliderRow("overlay", "WatchAlong", t("panel.along"), null, -12, 12, signedCm, { factor: 100, enabled: onHand }),
        sliderRow("overlay", "WatchLift", t("watch.lift"), null, -5, 10, signedCm, { factor: 100, enabled: onHand }),
      ], on),
      group(t("watch.style"), null, [
        segRow("overlay", "WatchStyle", t("watch.style"), null, [0, 1, 2].map(v => ({ value: v, label: t("wstyle." + v) }))),
        row(t("watch.elements"), t("watch.battery.help"), elements),
        sliderRow("overlay", "WatchSize", t("watch.size"), null, 3, 20, cm, { factor: 100 }),
        sliderRow("overlay", "WatchOpacity", t("watch.opacity"), null, 20, 100, pct, { factor: 100 }),
      ], on),
    ];
    const stage = [
      h("div", { class: "stage" }, h("img", { class: "watch", id: "watch-img", alt: t("nav.watch") })),
      h("div", { class: "stage-caption" }, h("span", { text: t("set.preview.sample") })),
    ];
    settingsLayout(page, form, stage);
  }

  // --- Fenêtre capturée --------------------------------------------------------------------------

  function renderWindowList() {
    const list = document.getElementById("win-list");
    if (!list) return;
    list.replaceChildren();
    if (!state.windows.length) { list.appendChild(h("div", { class: "muted small", style: { padding: "8px" }, text: t("win.none") })); return; }
    for (const w of state.windows) {
      const item = h("button", { type: "button", class: "win-item", "aria-pressed": String(!!w.selected), title: w.title,
        onclick: () => {
          if (w.closed) return;
          state.windows.forEach(x => (x.selected = false));
          w.selected = true;
          renderWindowList();
          send("chooseWindow", { id: w.id });
        } },
        h("span", { class: "t", text: w.title + (w.closed ? " (" + t("win.closed") + ")" : "") }), h("span", { class: "p", text: w.process || "" }));
      list.appendChild(item);
    }
  }

  function renderWindowPage(page) {
    refreshers = [];
    const onHand = () => get("overlay", "WindowAnchor") !== 2;
    const capture = !state.captureAvailable;
    const list = h("div", { class: "win-list", id: "win-list", role: "listbox", "aria-label": t("win.which") });
    const pick = h("div", { style: { display: "grid", gap: "10px", width: "100%" } }, list,
      h("div", null, h("button", { class: "btn", type: "button", disabled: capture, onclick: () => send("listWindows") }, icon("refresh"), t("win.refresh"))));
    const crop = (key, label) => sliderRow("desktop", key, label, null, 0, 45, pct);
    const form = [
      group(t("nav.window"), null, [switchCtl("overlay", "WindowEnabled", t("win.show"), t("win.show.help", hk("window_toggle")))]),
      group(t("win.which"), t("win.which.help"), [row("", null, pick, { stack: true })]),
      group(t("win.where"), t("win.where.help", hk("window_place")), [
        segRow("overlay", "WindowAnchor", t("win.where"), null, [{ value: 0, label: t("anchor.left") }, { value: 1, label: t("anchor.right") }, { value: 2, label: t("anchor.world") }]),
        sliderRow("overlay", "WindowDistance", t("win.distance"), null, 30, 300, meters, { factor: 100, enabled: () => !onHand() }),
        selectRow("overlay", "WindowPlacement", t("panel.placement"), null, placementOptions(), { enabled: onHand }),
        switchCtl("overlay", "WindowGazeOnly", t("panel.gaze"), null, { enabled: onHand }),
        sliderRow("overlay", "WindowAlong", t("panel.along"), null, -12, 12, signedCm, { factor: 100, enabled: onHand }),
        sliderRow("overlay", "WindowLift", t("panel.lift"), null, -5, 10, signedCm, { factor: 100, enabled: onHand }),
      ]),
      group(t("win.look"), null, [
        sliderRow("overlay", "WindowWidth", t("win.width"), null, 10, 150, cm, { factor: 100 }),
        sliderRow("desktop", "CaptureOpacity", t("win.opacity"), null, 20, 100, pct),
        sliderRow("desktop", "CaptureFps", t("win.fps"), null, 1, 15, v => num(v)),
      ]),
      group(t("win.crop"), t("win.crop.help"), [crop("CropLeft", t("crop.left")), crop("CropRight", t("crop.right")), crop("CropTop", t("crop.top")), crop("CropBottom", t("crop.bottom"))]),
    ];
    const stage = [
      h("div", { class: "stage" }, h("img", { id: "window-img", alt: t("set.preview"), hidden: true }), h("div", { class: "stage-empty", id: "window-empty", text: t("win.preview.wait") })),
      h("div", { class: "stage-caption" }, h("span", { id: "window-status" })),
    ];
    settingsLayout(page, form, stage);
    renderWindowList();
  }

  // --- Miroir du bureau ------------------------------------------------------------------------

  function renderDesktopPage(page) {
    refreshers = [];
    const on = () => !!get("desktop", "DesktopPanel");
    const form = [
      group(t("nav.desktop"), null, [switchCtl("desktop", "DesktopPanel", t("desk.show"), t("desk.show.help"))]),
      group(t("desk.where"), null, [
        selectRow("desktop", "Anchor", t("desk.anchor"), null, [0, 1, 2, 3, 4, 5, 6].map(v => ({ value: v, label: t("dpos." + v) }))),
        sliderRow("desktop", "Scale", t("desk.scale"), null, 25, 300, pct),
        sliderRow("desktop", "Opacity", t("desk.opacity"), null, 20, 100, pct),
        switchCtl("desktop", "TopMost", t("desk.topmost")),
      ], on),
    ];
    settingsLayout(page, form, panelStage());
  }

  // --- Vidéo du casque --------------------------------------------------------------------------

  // Durée en mm:ss (ou h:mm:ss).
  function clock(seconds) {
    const s = Math.max(0, Math.floor(seconds || 0));
    const two = n => String(n).padStart(2, "0");
    return s >= 3600 ? Math.floor(s / 3600) + ":" + two(Math.floor(s / 60) % 60) + ":" + two(s % 60) : two(Math.floor(s / 60)) + ":" + two(s % 60);
  }

  function bytesText(bytes) {
    const mb = (bytes || 0) / (1024 * 1024);
    return mb >= 1024 ? num(mb / 1024, 1) + " " + t("unit.gb") : num(mb, 0) + " " + t("unit.mb");
  }

  // Raccourcis : modificateurs << 16 | touche (code de touche virtuelle Windows). Modificateurs comme
  // RegisterHotKey : 1 Alt, 2 Ctrl, 4 Maj, 8 Windows. Mêmes règles que OverlaySettings.NormalizeHotkey et
  // Settings.ValidVideoHotkey (XRPerfSensors). Ceux du casque dans l'ordre de OverlaySettings.HotkeyNames.
  const MOD_ALT = 1, MOD_CTRL = 2, MOD_SHIFT = 4, MOD_WIN = 8;
  const CTRL_ALT = (MOD_CTRL | MOD_ALT) << 16;
  const HOTKEYS = [
    { name: "toggle", label: "key.toggle", def: CTRL_ALT | 0x4F },
    { name: "position", label: "key.position", def: CTRL_ALT | 0x50 },
    { name: "reset", label: "key.reset", def: CTRL_ALT | 0x52 },
    { name: "size_up", label: "key.sizeUp", def: CTRL_ALT | 0x21 },
    { name: "size_down", label: "key.sizeDown", def: CTRL_ALT | 0x22 },
    { name: "opacity_up", label: "key.opacityUp", def: CTRL_ALT | 0x24 },
    { name: "opacity_down", label: "key.opacityDown", def: CTRL_ALT | 0x23 },
    { name: "window_toggle", label: "key.window", def: CTRL_ALT | 0x46 },
    { name: "window_place", label: "key.place", def: CTRL_ALT | 0x47 },
    { name: "record", label: "key.record", def: CTRL_ALT | 0x53 },
    { name: "image", label: "key.image", def: CTRL_ALT | 0x49 },
  ];
  // Texte des raccourcis du casque du moment, pour les aides : { key, key2 }.
  function hk(name, name2) {
    const text = n => { const c = get("overlay", "Hotkey." + n); return c ? hotkeyText(c) : t("hk.unset"); };
    return name2 ? { key: text(name), key2: text(name2) } : { key: text(name) };
  }
  // Raccourci pris par un autre (casque ou vidéo) que `self` (« overlay:Hotkey.x » ou « desktop:VideoHotkey ») : son nom.
  function hotkeyOwner(combo, self) {
    if (!combo) return null;
    for (const x of HOTKEYS)
      if ("overlay:Hotkey." + x.name !== self && get("overlay", "Hotkey." + x.name) === combo) return t(x.label);
    if (self !== "desktop:VideoHotkey" && get("desktop", "VideoHotkey") === combo) return t("key.video");
    if (self !== "desktop:SnapHotkey" && get("desktop", "SnapHotkey") === combo) return t("key.snap");
    return null;
  }
  const MODIFIER_KEYS = [0x10, 0x11, 0x12, 0x5B, 0x5C, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5];
  function keyName(vk) {
    if (vk >= 0x70 && vk <= 0x87) return "F" + (vk - 0x6F);
    if ((vk >= 0x41 && vk <= 0x5A) || (vk >= 0x30 && vk <= 0x39)) return String.fromCharCode(vk);
    if (vk >= 0x60 && vk <= 0x69) return t("key.num") + " " + (vk - 0x60);
    const named = { 0x08: "key.backspace", 0x09: "key.tab", 0x0D: "key.enter", 0x13: "key.pause", 0x20: "key.space", 0x21: "key.pgup", 0x22: "key.pgdn",
      0x23: "key.end", 0x24: "key.home", 0x25: "key.left", 0x26: "key.up", 0x27: "key.right", 0x28: "key.down", 0x2D: "key.insert", 0x2E: "key.delete",
      0x6A: "key.numMul", 0x6B: "key.numAdd", 0x6D: "key.numSub", 0x6F: "key.numDiv", 0x91: "key.scroll" };
    return named[vk] ? t(named[vk]) : null;
  }
  // Morceaux du raccourci (« Ctrl », « Alt », « V »). Touche sans nom connu ici : celui donné par Windows (XRPerfSensors).
  function hotkeyParts(combo) {
    const mods = combo >> 16, vk = combo & 0xFFFF;
    if (!vk) return [];
    const parts = [];
    if (mods & MOD_CTRL) parts.push("Ctrl");
    if (mods & MOD_ALT) parts.push("Alt");
    if (mods & MOD_SHIFT) parts.push(t("key.shift"));
    if (mods & MOD_WIN) parts.push("Win");
    const own = keyName(vk);
    const windows = state.video && state.video.hotkeyText ? state.video.hotkeyText.split("+").pop() : "";
    parts.push(own || windows || "0x" + vk.toString(16).toUpperCase());
    return parts;
  }
  const hotkeyText = combo => hotkeyParts(combo).join("+");
  // null si le raccourci convient, sinon la raison.
  function hotkeyProblem(mods, vk, self) {
    if (MODIFIER_KEYS.includes(vk)) return null;  // modificateur seul : on attend la touche
    if (!mods && !(vk >= 0x70 && vk <= 0x87)) return t("vid.key.needMod");
    const owner = hotkeyOwner((mods << 16) | vk, self);
    if (owner) return t("vid.key.used", { key: hotkeyText((mods << 16) | vk), name: owner });
    return null;
  }

  // Saisie d'un raccourci : le prochain appui (avec ses modificateurs) devient le raccourci. Échap annule,
  // Retour arrière ou Suppr seuls retirent le raccourci. L'ancien est suspendu pendant la saisie.
  var hotkeyCapture = null;  // var : connue avant cette ligne (go() peut être appelé plus tôt)
  function stopHotkeyCapture() {
    if (!hotkeyCapture) return;
    const c = hotkeyCapture;
    hotkeyCapture = null;
    document.removeEventListener("keydown", c.onKey, true);
    document.removeEventListener("keyup", c.onKeyUp, true);
    clearTimeout(c.timer);
    send("videoHotkeyCapture", { on: false });
    c.done();
  }
  // Ligne de réglage d'un raccourci. `scope`, `key` : « desktop », « VideoHotkey » (raccourci global de
  // XRPerfSensors, suspendu pendant la saisie) ou « overlay », « Hotkey.x » (lu en jeu par la layer et XRPerfVR).
  // `def` : raccourci par défaut (bouton « Par défaut »), absent pour la vidéo.
  function hotkeyRow(scope, key, label, help, def) {
    scope = scope || "desktop";
    key = key || "VideoHotkey";
    const self = scope + ":" + key;
    const global = scope === "desktop";
    const button = h("button", { class: "btn hotkey-btn", type: "button", "aria-label": label || t("vid.key") });
    const clear = h("button", { class: "btn", type: "button", text: t("vid.key.clear") });
    const reset = def ? h("button", { class: "btn", type: "button", text: t("hk.default") }) : null;
    const note = h("div", { class: "video-note", hidden: true });
    const show = () => {
      const combo = get(scope, key) || 0;
      button.replaceChildren(...(combo ? hotkeyParts(combo).map(x => h("kbd", { text: x })) : [h("span", { text: t("vid.key.none") })]));
      button.classList.remove("capturing");
      clear.disabled = !combo || locked("overlay");
      button.disabled = locked("overlay");
      if (reset) reset.disabled = combo === def || locked("overlay");
    };
    const live = mods => {
      const parts = [];
      if (mods & MOD_CTRL) parts.push("Ctrl");
      if (mods & MOD_ALT) parts.push("Alt");
      if (mods & MOD_SHIFT) parts.push(t("key.shift"));
      if (mods & MOD_WIN) parts.push("Win");
      button.replaceChildren(...parts.map(x => h("kbd", { text: x })), h("span", { text: (parts.length ? "+ " : "") + t("vid.key.press") }));
    };
    const modsOf = e => (e.ctrlKey ? MOD_CTRL : 0) | (e.altKey ? MOD_ALT : 0) | (e.shiftKey ? MOD_SHIFT : 0) | (e.metaKey ? MOD_WIN : 0);
    button.addEventListener("click", () => {
      if (hotkeyCapture) { stopHotkeyCapture(); return; }
      note.hidden = true;
      button.classList.add("capturing");
      live(0);
      const onKey = e => {
        e.preventDefault();
        e.stopPropagation();
        if (!document.body.contains(button)) { stopHotkeyCapture(); return; }
        const mods = modsOf(e), vk = e.keyCode;
        if (vk === 0x1B && !mods) { stopHotkeyCapture(); return; }
        if ((vk === 0x08 || vk === 0x2E) && !mods) { setValue(scope, key, 0); stopHotkeyCapture(); return; }
        if (MODIFIER_KEYS.includes(vk)) { live(mods); return; }
        const problem = hotkeyProblem(mods, vk, self);
        if (problem) {
          note.textContent = problem;
          note.hidden = false;
          live(0);
          return;
        }
        setValue(scope, key, (mods << 16) | vk);
        stopHotkeyCapture();
      };
      const onKeyUp = e => { if (MODIFIER_KEYS.includes(e.keyCode)) { e.preventDefault(); live(modsOf(e)); } };
      hotkeyCapture = { onKey, onKeyUp, done: show, timer: setTimeout(stopHotkeyCapture, 15000) };
      document.addEventListener("keydown", onKey, true);
      document.addEventListener("keyup", onKeyUp, true);
      if (global) send("videoHotkeyCapture", { on: true });
    });
    button.addEventListener("blur", () => setTimeout(() => { if (hotkeyCapture && document.activeElement !== button) stopHotkeyCapture(); }, 0));
    clear.addEventListener("click", () => { stopHotkeyCapture(); setValue(scope, key, 0); note.hidden = true; });
    if (reset) reset.addEventListener("click", () => {
      stopHotkeyCapture();
      const owner = hotkeyOwner(def, self);
      if (owner) {
        note.textContent = t("vid.key.used", { key: hotkeyText(def), name: owner });
        note.hidden = false;
        return;
      }
      note.hidden = true;
      setValue(scope, key, def);
    });
    show();
    refreshers.push(() => { if (!hotkeyCapture) show(); });
    const r = row(label || t("vid.key"), help === undefined ? t("vid.key.help") : help, h("div", { class: "row-buttons" }, button, clear, reset));
    r.querySelector(".row-label").parentNode.appendChild(note);
    return r;
  }

  function videoEnabled() { return !!get("overlay", "VideoCapture") && !state.unavailable; }

  // Débit d'une qualité (Mbit/s), même calcul que VideoRecorder.PresetBitrate (XRPerfSensors) : référence
  // H.264 1080p à 60 images/s, H.265 et AV1 à 70 % et 60 %, selon les pixels (exposant 0,8) et la cadence (0,75).
  const VIDEO_QUALITIES = ["light", "balanced", "high", "max", "custom"];
  function presetBitrate(quality, codec, height, aspectText, fps) {
    const reference = { light: 10, balanced: 16, max: 40 }[quality] || 24;
    const codecFactor = codec === "hevc" ? 0.7 : codec === "av1" ? 0.6 : 1;
    const aspect = aspectText === "4:3" ? 4 / 3 : aspectText === "1:1" ? 1 : 16 / 9;
    const pixels = (height * Math.round(height * aspect)) / (1920 * 1080);
    return Math.max(2, Math.min(150, Math.round(reference * codecFactor * Math.pow(pixels, 0.8) * Math.pow(fps / 60, 0.75))));
  }
  function videoBitrate() {
    const q = get("desktop", "VideoQuality");
    if (q === "custom") return get("desktop", "VideoBitrate");
    return presetBitrate(q, get("desktop", "VideoCodec"), get("desktop", "VideoHeight"), get("desktop", "VideoAspect"), get("desktop", "VideoFps"));
  }
  // Taille d'une minute de vidéo (1 Mbit/s : 7,5 Mo par minute).
  function perMinute(mbps) {
    const mb = mbps * 7.5;
    return mb >= 1000 ? num(mb / 1000, 1) + " " + t("unit.gb") : num(mb, 0) + " " + t("unit.mb");
  }

  // Ligne dont l'explication dépend des réglages (mise à jour à chaque changement).
  function liveHelp(r, text) {
    const help = r.querySelector(".row-help");
    const f = () => { help.textContent = text(); };
    refreshers.push(f);
    f();
    return r;
  }

  // Liste des sorties (son du jeu) ou des entrées (micro) : « par défaut » puis celles de Windows.
  function deviceRow(key, label, kind, enabled) {
    const sel = h("select", { class: "select", "aria-label": label, "data-devices": kind, "data-key": key });
    sel.addEventListener("change", () => setValue("desktop", key, sel.value));
    const r = row(label, null, sel, { enabled });
    fillDevice(sel);
    return r;
  }

  function fillDevice(sel) {
    const kind = sel.dataset.devices, key = sel.dataset.key;
    const list = (state.videoDevices && state.videoDevices[kind]) || [];
    const current = get("desktop", key) || "";
    const options = [{ id: "", name: t(kind === "render" ? "vid.device.defaultOut" : "vid.device.defaultIn") }].concat(list);
    // Périphérique choisi mais débranché : gardé dans la liste, signalé.
    if (current && !list.some(d => d.id === current)) options.push({ id: current, name: t("vid.device.missing") });
    sel.replaceChildren(...options.map(d => h("option", { value: d.id, text: d.name })));
    sel.value = current;
    sel.disabled = locked("overlay");
  }

  function fillDevices() { document.querySelectorAll("select[data-devices]").forEach(fillDevice); }

  // Direct (Twitch, YouTube) : sortie choisie, débit plafonné selon le service (Settings.MaxStreamBitrate).
  const streaming = () => get("desktop", "VideoTarget") === "stream";
  // Services du direct : un, ou les deux (Twitch puis YouTube, dans l'ordre des sorties de l'enregistreur).
  const streamTargets = () => {
    const s = get("desktop", "StreamService");
    return s === "both" ? ["twitch", "youtube"] : [s === "youtube" ? "youtube" : "twitch"];
  };
  const streamUses = service => streamTargets().indexOf(service) >= 0;
  const streamMax = service => service === "youtube" ? 30000 : 8000;
  const serviceKey = service => service === "youtube" ? "Youtube" : "Twitch";  // StreamKeyTwitch, StreamTwitchHeight...
  const servicesText = () => streamTargets().map(x => t("vid.stream.service." + x)).join(" + ");

  // Clé de stream : jamais renvoyée par XRPerfSensors, seulement « enregistrée ou non ». Saisie gardée chiffrée.
  function streamKeyRow(service) {
    const streamKeyName = () => "StreamKey" + serviceKey(service);
    const input = h("input", { class: "text-input", style: { width: "260px" }, type: "password", maxlength: "200", spellcheck: "false", autocomplete: "off",
      "aria-label": t("vid.stream.key") });
    const save = h("button", { class: "btn", type: "button", text: t("vid.stream.key.save"), disabled: true });
    const clear = h("button", { class: "btn", type: "button", text: t("vid.stream.key.clear") });
    const status = () => !!get("desktop", streamKeyName() + "Set");
    const store = value => {
      send("set", { scope: "desktop", key: streamKeyName(), value });
      state.desktop[streamKeyName() + "Set"] = value.length > 0;
      input.value = "";
      refreshers.forEach(f => f());
    };
    input.addEventListener("input", () => { save.disabled = input.value.trim().length < 8; });
    input.addEventListener("keydown", e => { if (e.key === "Enter" && !save.disabled) store(input.value.trim()); });
    save.addEventListener("click", () => store(input.value.trim()));
    clear.addEventListener("click", () => store(""));
    const r = row(t("vid.stream.key"), " ", h("div", { class: "row-buttons" }, input, save, clear));
    const help = r.querySelector(".row-help");
    const f = () => {
      const set = status();
      help.textContent = t(set ? "vid.stream.key.set" : "vid.stream.key.none." + service, { service: t("vid.stream.service." + service) });
      input.placeholder = t(set ? "vid.stream.key.replace" : "vid.stream.key.placeholder");
      clear.hidden = !set;
      save.disabled = input.value.trim().length < 8;
    };
    refreshers.push(f);
    f();
    return r;
  }

  function renderVideoPage(page) {
    refreshers = [];
    const v = state.video || {};
    const codecs = v.codecs == null ? -1 : v.codecs;
    const codecOptions = ["h264", "hevc", "av1"].map((c, i) => ({ value: c, label: t("vid.codec." + c) + (codecs >= 0 && !(codecs & (1 << i)) ? " (" + t("vid.unavailable") + ")" : "") }));
    const folderRow = row(t("vid.folder"), get("desktop", "VideoFolder") || v.folder || " ", h("div", { class: "row-buttons" },
      h("button", { class: "btn", type: "button", onclick: () => send("videoPickFolder") }, t("vid.folder.change")),
      h("button", { class: "btn", type: "button", onclick: () => send("videoOpenFolder") }, icon("folder"), t("vid.folder.open"))));
    folderRow.querySelector(".row-help").id = "video-folder";
    const form = [
      group(t("vid.title"), null, [
        switchCtl("overlay", "VideoCapture", t("vid.enable"), t("vid.enable.help")),
        liveHelp(segRow("desktop", "VideoTarget", t("vid.target"), " ", [{ value: "file", label: t("vid.target.file") }, { value: "stream", label: t("vid.target.stream") }],
          { enabled: videoEnabled }), () => t(streaming() ? "vid.target.help.stream" : "vid.target.help.file")),
        h("div", { class: "video-card", id: "video-card", "aria-live": "polite" }),
        segRow("overlay", "VideoSource", t("vid.source"), t("vid.source.help"), [{ value: "final", label: t("vid.source.final") }, { value: "game", label: t("vid.source.game") }],
          { enabled: videoEnabled }),
      ]),
      group(t("vid.stream"), t("vid.stream.help"), [
        liveHelp(segRow("desktop", "StreamService", t("vid.stream.service"), " ", ["twitch", "youtube", "both"].map(x => ({ value: x, label: t("vid.stream.service." + x) }))),
          () => t(get("desktop", "StreamService") === "both" ? "vid.stream.service.help.both" : "vid.stream.service.help")),
        segRow("desktop", "StreamFps", t("vid.fps"), null, [30, 60].map(x => ({ value: x, label: String(x) }))),
      ], () => videoEnabled() && streaming()),
      ...["twitch", "youtube"].map(service => group(t("vid.stream.service." + service), t("vid.stream.for." + service), [
        streamKeyRow(service),
        service === "youtube"
          ? liveHelp(selectRow("desktop", "StreamCodec", t("vid.codec"), " ", codecOptions), () => t("vid.stream.codec.help"))
          : row(t("vid.codec"), t("vid.stream.codec.twitch"), h("output", { text: "H.264" })),
        selectRow("desktop", "Stream" + serviceKey(service) + "Height", t("vid.resolution"), null,
          (service === "youtube" ? [720, 900, 1080, 1440] : [720, 900, 1080]).map(x => ({ value: x, label: x + "p" }))),
        liveHelp(sliderRow("desktop", "Stream" + serviceKey(service) + "Bitrate", t("vid.bitrate"), " ", 1500, 30000,
          x => num(Math.min(x, streamMax(service))) + " " + t("unit.kbps"), { step: 250 }),
          () => t(get("desktop", "Stream" + serviceKey(service) + "Bitrate") > streamMax(service) ? "vid.stream.bitrate.capped" : "vid.stream.bitrate.help." + service,
            { max: num(streamMax(service)) })),
      ], () => videoEnabled() && streaming() && streamUses(service))),
      group(t("vid.image"), t("vid.image.help"), [
        selectRow("desktop", "VideoCodec", t("vid.codec"), t("vid.codec.help"), codecOptions, { enabled: () => !streaming() }),
        selectRow("desktop", "VideoHeight", t("vid.resolution"), null, [720, 1080, 1440, 2160].map(x => ({ value: x, label: x === 2160 ? "2160p (4K)" : x + "p" })),
          { enabled: () => !streaming() }),
        segRow("desktop", "VideoFps", t("vid.fps"), t("vid.fps.help"), [30, 60, 72, 90].map(x => ({ value: x, label: String(x) })), { enabled: () => !streaming() }),
        liveHelp(selectRow("desktop", "VideoQuality", t("vid.quality"), " ", VIDEO_QUALITIES.map(q => ({ value: q, label: t("vid.quality." + q) }))),
          () => t(get("desktop", "VideoQuality") === "custom" ? "vid.quality.helpCustom" : "vid.quality.help", { mbps: num(videoBitrate()), size: perMinute(videoBitrate()) })),
        sliderRow("desktop", "VideoBitrate", t("vid.bitrate"), t("vid.bitrate.help"), 2, 150, x => num(x) + " " + t("unit.mbps"),
          { enabled: () => get("desktop", "VideoQuality") === "custom" && !streaming() }),
        segRow("desktop", "VideoEye", t("vid.eye"), null, [{ value: 0, label: t("vid.eye.left") }, { value: 1, label: t("vid.eye.right") }]),
        segRow("desktop", "VideoAspect", t("vid.aspect"), null, ["16:9", "4:3", "1:1"].map(a => ({ value: a, label: a }))),
        sliderRow("desktop", "VideoField", t("vid.field"), t("vid.field.help"), 40, 100, pct),
      ], videoEnabled),
      group(t("vid.motion"), t("vid.motion.help"), [
        liveHelp(segRow("desktop", "VideoStabilize", t("vid.stabilize"), " ", ["off", "light", "strong"].map(o => ({ value: o, label: t("vid.stabilize." + o) }))),
          () => t("vid.stabilize.help." + (get("desktop", "VideoStabilize") || "off"))),
        switchCtl("desktop", "VideoLevel", t("vid.level"), t("vid.level.help")),
      ], videoEnabled),
      group(t("vid.panel"), t("vid.panel.help"), [
        liveHelp(segRow("desktop", "VideoOverlay", t("vid.overlay"), " ", ["off", "place", "corner"].map(o => ({ value: o, label: t("vid.overlay." + o) }))),
          () => t("vid.overlay.help." + (get("desktop", "VideoOverlay") || "off"))),
        selectRow("desktop", "VideoOverlayCorner", t("vid.corner"), null, ["topleft", "topright", "bottomleft", "bottomright"].map(c => ({ value: c, label: t("vid.corner." + c) })),
          { enabled: () => get("desktop", "VideoOverlay") === "corner" }),
        sliderRow("desktop", "VideoOverlaySize", t("vid.overlaySize"), t("vid.overlaySize.help"), 15, 50, pct,
          { enabled: () => get("desktop", "VideoOverlay") === "corner" }),
      ], videoEnabled),
      group(t("vid.sound"), t("vid.sound.help"), [
        switchCtl("desktop", "VideoGameAudio", t("vid.game"), t("vid.game.help")),
        deviceRow("VideoRenderDevice", t("vid.output"), "render", () => !!get("desktop", "VideoGameAudio")),
        switchCtl("desktop", "VideoMic", t("vid.mic"), null),
        deviceRow("VideoCaptureDevice", t("vid.input"), "capture", () => !!get("desktop", "VideoMic")),
      ], videoEnabled),
      group(t("vid.files"), null, [
        hotkeyRow(),
        folderRow,
      ], videoEnabled),
      group(t("snap.title"), t("snap.help"), [
        h("div", { class: "video-card", id: "snap-card", "aria-live": "polite" }),
        segRow("desktop", "SnapFormat", t("snap.format"), t("snap.format.help"), [{ value: "png", label: "PNG" }, { value: "jpeg", label: "JPEG" }]),
        sliderRow("desktop", "SnapQuality", t("snap.quality"), t("snap.quality.help"), 50, 100, pct, { enabled: () => get("desktop", "SnapFormat") === "jpeg" }),
        selectRow("desktop", "SnapHeight", t("vid.resolution"), t("snap.resolution.help"),
          [1080, 1440, 2160, 2880].map(x => ({ value: x, label: x === 2160 ? "2160p (4K)" : x === 2880 ? "2880p (5K)" : x + "p" }))),
        hotkeyRow("desktop", "SnapHotkey", t("vid.key"), t("snap.key.help")),
        snapFolderRow(),
      ], videoEnabled),
      group(t("vid.obs"), t("vid.obs.help"), [
        switchCtl("desktop", "VideoObs", t("vid.obs.enable"), t("vid.obs.enable.help")),
        row(t("vid.obs.state"), null, h("span", { class: "row-help", id: "obs-state", "aria-live": "polite" }), { enabled: () => !!get("desktop", "VideoObs") }),
      ], videoEnabled),
    ];
    // Onglets : l'état et le bouton (premier groupe) restent visibles sur chacun.
    const [status, stream, twitch, youtube, image, motion, overlay, sound, files, snap, obs] = form;
    const byTab = { rec: [image, files], live: [stream, twitch, youtube], snap: [snap], sound: [sound], camera: [motion, overlay], obs: [obs] };
    page.appendChild(tabBar("video"));
    settingsLayout(page, [status].concat(byTab[state.tab.video] || byTab.rec), null);
    refreshers.push(updateVideo);
    updateVideo();
    if (!state.videoDevices) send("videoDevices");
  }

  function snapFolderRow() {
    const r = row(t("snap.folder"), get("desktop", "SnapFolder") || " ", h("div", { class: "row-buttons" },
      h("button", { class: "btn", type: "button", onclick: () => send("snapPickFolder") }, t("vid.folder.change")),
      h("button", { class: "btn", type: "button", onclick: () => send("snapOpenFolder") }, icon("folder"), t("vid.folder.open"))));
    r.querySelector(".row-help").id = "snap-folder";
    return r;
  }

  // Carte des captures d'écran : bouton, nombre prises, dernier fichier.
  function updateSnap() {
    const card = document.getElementById("snap-card");
    const folder = document.getElementById("snap-folder");
    const v = state.video || {};
    if (folder) folder.textContent = get("desktop", "SnapFolder") || v.snapFolder || "";
    if (!card) return;
    const key = get("desktop", "SnapHotkey");
    const detail = [key ? t("snap.ready.key", { key: hotkeyText(key) }) : t("snap.ready.nokey")];
    if (v.snapCount) detail.push(t("snap.count", { n: num(v.snapCount) }));
    const last = v.snapLast ? t("snap.last", { file: v.snapLast.split("\\").pop() }) : "";
    const notes = [];
    if (v.snapFailed) notes.push(t("snap.failed", { n: num(v.snapFailed) }));
    if (key && v.snapHotkeyOk === false) notes.push(t("vid.key.taken", { key: hotkeyText(key) }));
    if (v.available !== false && v.snapSupported === false) notes.push(t("snap.unsupported"));
    card.className = "video-card";
    card.replaceChildren(
      h("div", { class: "video-state" }, h("div", null, h("div", { class: "video-title", text: t("snap.ready") }),
        h("div", { class: "row-help", text: detail.join(" · ") }),
        last ? h("div", { class: "row-help", text: last, title: v.snapLast }) : null,
        notes.map(n => h("div", { class: "video-note", text: n })))),
      h("button", { class: "btn primary", type: "button", disabled: !videoEnabled() || v.available === false || v.snapSupported === false,
        onclick: () => send("snapshot") }, icon("image"), t("snap.take")));
  }

  // Carte d'état : ce qui se passe, et le bouton qui va avec. Mise à jour chaque seconde sans redessiner la page.
  function updateVideo() {
    updateSnap();
    const card = document.getElementById("video-card");
    const folder = document.getElementById("video-folder");
    const v = state.video || { state: 0, available: true };
    if (folder) folder.textContent = get("desktop", "VideoFolder") || v.folder || "";
    const obs = document.getElementById("obs-state");
    if (obs) obs.textContent = !get("desktop", "VideoObs") ? t("vid.obs.state.off") : v.obsShown ? t(v.obsFed ? "vid.obs.state.fed" : "vid.obs.state.waiting")
      : v.obsPlugin ? t("vid.obs.state.notShown") : t("vid.obs.state.missing");
    if (!card) return;
    const enabled = videoEnabled();
    const key = get("desktop", "VideoHotkey");
    const hotkey = key ? hotkeyText(key) : "";
    let title, detail = [], cls = "";
    const notes = [];
    if (!v.available) {
      title = t("vid.state.missing");
    } else if (v.state === 2 && v.streaming) {
      cls = " on";
      // Une ou deux sorties : état, débit, pertes et reconnexions de chacune.
      const outs = streamTargets().map((service, i) => {
        const p = i === 0 ? "stream" : "stream2";
        return { name: t("vid.stream.service." + service), state: v[p + "State"] || 0, kbps: v[p + "Kbps"] || 0, dropped: v[p + "Dropped"] || 0,
          reconnects: v[p + "Reconnects"] || 0 };
      });
      const reconnecting = outs.some(o => o.state === 3);
      title = (reconnecting ? t("vid.live.reconnecting") : "LIVE") + " " + clock(v.seconds);
      const audio = v.audio || 0;
      const sound = (audio & 3) === 3 ? t("vid.audio.both") : audio & 1 ? t("vid.audio.game") : audio & 2 ? t("vid.audio.mic") : t("vid.audio.none");
      const outText = o => o.name + " " + (o.state === 4 ? t("vid.live.stopped") : o.state === 3 ? t("vid.live.reconnecting").toLowerCase() : num(o.kbps) + " " + t("unit.kbps"));
      detail = [...outs.map(outText), outs.length === 1 ? v.encoder || "" : "", sound].filter(Boolean);
      if (audio & 4) notes.push(t("vid.audio.gameFailed"));
      if (audio & 8) notes.push(t("vid.audio.micFailed"));
      for (const o of outs) {
        const prefix = outs.length > 1 ? o.name + " : " : "";
        if (o.state === 4) notes.push(prefix + t("vid.live.outputStopped"));
        if (o.dropped > 0) notes.push(prefix + t("vid.live.dropped", { n: num(o.dropped) }));
        if (o.reconnects > 0) notes.push(prefix + t("vid.live.reconnects", { n: num(o.reconnects) }));
      }
    } else if (v.state === 1 && v.streaming && v.streamState === 1) {
      cls = " wait";
      title = t("vid.live.connecting");
    } else if (v.state === 2) {
      cls = " on";
      title = "REC " + clock(v.seconds);
      const audio = v.audio || 0;
      const sound = (audio & 3) === 3 ? t("vid.audio.both") : audio & 1 ? t("vid.audio.game") : audio & 2 ? t("vid.audio.mic") : t("vid.audio.none");
      detail = [v.game, bytesText(v.bytes), v.encoder || t("vid.codec." + get("desktop", "VideoCodec")), sound].filter(Boolean);
      if (audio & 4) notes.push(t("vid.audio.gameFailed"));
      if (audio & 8) notes.push(t("vid.audio.micFailed"));
      if (v.frames > 120 && v.dropped > v.frames / 20) notes.push(t("vid.dropped"));
    } else if (v.state === 1) {
      cls = " wait";
      title = t("vid.state.waiting");
      detail = [t("vid.state.waiting.help")];
    } else if (v.state === 3) {
      title = t("vid.state.finishing");
    } else {
      const live = streaming();
      title = enabled ? t(live ? "vid.live.ready" : "vid.state.ready", { service: servicesText() }) : t("vid.state.off");
      if (enabled) detail = [hotkey ? t(live ? "vid.live.ready.key" : "vid.state.ready.key", { key: hotkey }) : t(live ? "vid.live.ready.nokey" : "vid.state.ready.nokey")];
      if (enabled && live)
        for (const service of streamTargets())
          if (!get("desktop", "StreamKey" + serviceKey(service) + "Set")) notes.push(t("vid.live.nokey", { service: t("vid.stream.service." + service) }));
      if (v.reason) notes.push(v.reason);
      else if (v.file && !v.streaming) detail.push(t("vid.last", { file: v.file.split("\\").pop() }));
    }
    if (enabled && key && v.hotkeyOk === false) notes.push(t("vid.key.taken", { key: hotkey }));
    const recording = v.state === 1 || v.state === 2;
    const button = recording
      ? h("button", { class: "btn danger", type: "button", onclick: () => send("videoStop") }, t("vid.stop"))
      : h("button", { class: "btn primary", type: "button", disabled: !enabled || !v.available || v.state === 3, onclick: () => send("videoStart") }, icon("video"),
          t(streaming() ? "vid.live.start" : "vid.start"));
    card.className = "video-card" + cls;
    card.replaceChildren(
      h("div", { class: "video-state" }, h("i", { class: "rec-dot" }), h("div", null, h("div", { class: "video-title", text: title }),
        detail.length ? h("div", { class: "row-help", text: detail.join(" · ") }) : null,
        notes.map(n => h("div", { class: "video-note", text: n })))),
      button);
  }

  // --- Raccourcis --------------------------------------------------------------------------------

  // Raccourcis du casque (lus en jeu par la layer et XRPerfVR, quelle que soit la fenêtre active) et de la vidéo.
  function renderHotkeysPage(page) {
    refreshers = [];
    const headset = HOTKEYS.map(x => hotkeyRow("overlay", "Hotkey." + x.name, t(x.label), null, x.def));
    const form = [
      group(t("hk.headset"), t("hk.headset.help"), headset.concat([
        row(t("hk.resetAll"), t("hk.resetAll.help"),
          h("button", { class: "btn", type: "button", disabled: locked("overlay"), onclick: () => { stopHotkeyCapture(); send("resetHotkeys"); } }, t("hk.resetAll.button"))),
      ])),
      group(t("hk.video"), null, [hotkeyRow("desktop", "VideoHotkey", t("key.video")), hotkeyRow("desktop", "SnapHotkey", t("key.snap"), t("snap.key.help"))]),
    ];
    settingsLayout(page, form, null);
  }

  // --- Image -----------------------------------------------------------------------------------

  // Filtres d'image : dans le casque (jeux OpenXR, au bas de la chaîne, après Pimax NR ou DLSS) ou
  // seulement dans la vidéo (aussi les jeux SteamVR). Tout est neutre par défaut.
  function renderImagePage(page) {
    refreshers = [];
    const mode = () => get("overlay", "ImageMode") || "off";
    const on = () => mode() !== "off";
    const sharpening = () => on() && (get("overlay", "ImageSharpen") || "none") !== "none";
    const signed = v => (v > 0 ? "+" : "") + num(v);
    // Preset dont les réglages sont ceux du moment (sinon « Personnalisé »).
    const presets = get("overlay", "ImagePresets") || [];
    const presetName = p => p.builtin ? t("img.preset." + p.id) : p.name;
    const current = () => presets.find(p => Object.keys(p.values).every(k => get("overlay", k) === p.values[k]));
    const presetSelect = h("select", { class: "select", "aria-label": t("img.preset") });
    const fillPresets = () => {
      const now = current();
      presetSelect.replaceChildren(
        h("option", { value: "", text: t("img.preset.custom"), hidden: !!now }),
        h("optgroup", { label: t("img.preset.builtin") }, presets.filter(p => p.builtin).map(p => h("option", { value: p.id, text: presetName(p) }))),
        presets.some(p => !p.builtin) ? h("optgroup", { label: t("img.preset.mine") }, presets.filter(p => !p.builtin).map(p => h("option", { value: p.id, text: p.name }))) : null);
      presetSelect.value = now ? now.id : "";
      presetSelect.disabled = locked("overlay");
      remove.hidden = !now || now.builtin;
    };
    presetSelect.addEventListener("change", () => { if (presetSelect.value) send("imagePresetApply", { id: presetSelect.value }); });
    const remove = h("button", { class: "btn", type: "button", text: t("img.preset.delete") });
    remove.addEventListener("click", () => { const now = current(); if (now && !now.builtin) send("imagePresetDelete", { id: now.id }); });
    const nameInput = h("input", { class: "text-input", style: { width: "200px" }, type: "text", maxlength: "40", spellcheck: "false", autocomplete: "off", placeholder: t("img.preset.name"), "aria-label": t("img.preset.name") });
    const save = h("button", { class: "btn", type: "button", text: t("img.preset.save"), disabled: true });
    const doSave = () => { const name = nameInput.value.trim(); if (name) { send("imagePresetSave", { name }); nameInput.value = ""; save.disabled = true; } };
    nameInput.addEventListener("input", () => { save.disabled = !nameInput.value.trim() || locked("overlay"); });
    nameInput.addEventListener("keydown", e => { if (e.key === "Enter") doSave(); });
    save.addEventListener("click", doSave);
    nameInput.disabled = locked("overlay");
    fillPresets();
    refreshers.push(fillPresets);
    const form = [
      group(t("img.presets"), t("img.presets.help"), [
        row(t("img.preset"), null, h("div", { class: "row-buttons" }, presetSelect, remove)),
        row(t("img.preset.saveAs"), t("img.preset.saveAs.help"), h("div", { class: "row-buttons" }, nameInput, save)),
      ]),
      group(t("nav.image"), t("img.help"), [
        liveHelp(segRow("overlay", "ImageMode", t("img.mode"), " ", ["off", "headset", "video"].map(m => ({ value: m, label: t("img.mode." + m) }))),
          () => t("img.mode.help." + mode())),
        row(t("img.compare"), t("img.compare.help"), get("overlay", "Hotkey.image")
          ? h("span", { class: "combo" }, hotkeyParts(get("overlay", "Hotkey.image")).map(x => h("kbd", { text: x })))
          : h("span", { class: "row-help", text: t("hk.unset") }), { enabled: on }),
        row(t("img.reset"), t("img.reset.help"), h("button", { class: "btn", type: "button", disabled: locked("overlay"), onclick: () => send("resetImage") }, t("img.reset.button")),
          { enabled: on }),
      ]),
      group(t("img.sharpen"), t("img.sharpen.help"), [
        liveHelp(selectRow("overlay", "ImageSharpen", t("img.sharpen.method"), " ", ["none", "cas", "rcas", "luma"].map(m => ({ value: m, label: t("img.sharpen." + m) }))),
          () => t("img.sharpen.help." + (get("overlay", "ImageSharpen") || "none"))),
        sliderRow("overlay", "ImageSharpness", t("img.sharpness"), t("img.sharpness.help"), 0, 200, pct, { enabled: sharpening }),
        sliderRow("overlay", "ImageSharpenLimit", t("img.sharpenLimit"), t("img.sharpenLimit.help"), 1, 50, v => num(v / 100, 2),
          { enabled: () => on() && get("overlay", "ImageSharpen") === "luma" }),
        segRow("overlay", "ImageSharpenRadius", t("img.sharpenRadius"), t("img.sharpenRadius.help"),
          [1, 2, 3].map(r => ({ value: r, label: t("img.sharpenRadius.px", { n: r }) })), { enabled: sharpening }),
      ], on),
      group(t("img.colors"), t("img.colors.help"), [
        sliderRow("overlay", "ImageBrightness", t("img.brightness"), null, -50, 50, signed),
        sliderRow("overlay", "ImageContrast", t("img.contrast"), null, -50, 50, signed),
        sliderRow("overlay", "ImageBlack", t("img.black"), t("img.black.help"), 0, 25, pct),
        sliderRow("overlay", "ImageWhite", t("img.white"), t("img.white.help"), 75, 100, pct),
        sliderRow("overlay", "ImageClarity", t("img.clarity"), t("img.clarity.help"), 0, 100, num),
        sliderRow("overlay", "ImageHdr", t("img.hdr"), t("img.hdr.help"), 0, 100, num),
        sliderRow("overlay", "ImageSaturation", t("img.saturation"), null, -100, 100, signed),
        sliderRow("overlay", "ImageVibrance", t("img.vibrance"), t("img.vibrance.help"), 0, 100, num),
        sliderRow("overlay", "ImageGamma", t("img.gamma"), t("img.gamma.help"), 50, 200, v => num(v / 100, 2)),
        sliderRow("overlay", "ImageShadows", t("img.shadows"), t("img.shadows.help"), 0, 100, num),
        sliderRow("overlay", "ImageTemperature", t("img.temperature"), t("img.temperature.help"), -50, 50, signed),
      ], on),
      group(t("img.comfort"), null, [
        sliderRow("overlay", "ImageDeband", t("img.deband"), t("img.deband.help"), 0, 100, num),
        selectRow("overlay", "ImageColorblind", t("img.colorblind"), t("img.colorblind.help"), ["none", "protan", "deutan", "tritan"].map(m => ({ value: m, label: t("img.colorblind." + m) }))),
      ], on),
      group(t("img.advanced"), null, [
        switchCtl("overlay", "ImageAllowed", t("img.allowed"), t("img.allowed.help")),
      ]),
    ];
    settingsLayout(page, form, null);
  }

  // --- Mesures --------------------------------------------------------------------------------

  // Mode des rapports : l'explication suit le mode choisi.
  function reportModeRow() {
    const modes = ["auto", "manual", "off"];
    const r = segRow("overlay", "ReportMode", t("meas.mode"), " ", modes.map(m => ({ value: m, label: t("meas.mode." + m) })), { stack: true });
    const help = r.querySelector(".row-help");
    const f = () => { help.textContent = t("meas.mode.help." + (modes.includes(get("overlay", "ReportMode")) ? get("overlay", "ReportMode") : "auto"), hk("record")); };
    refreshers.push(f);
    f();
    return r;
  }

  function renderMeasuresPage(page) {
    refreshers = [];
    const folder = state.reportFolders[0] || state.reportsFolder || "";
    const form = [
      group(t("meas.gpu"), t("set.restart"), [
        switchCtl("overlay", "GpuTiming", t("meas.gpu.label"), t("meas.gpu.help")),
        h("details", { class: "more" }, h("summary", { text: t("meas.gpu.more") }), h("p", { text: t("meas.gpu.long") })),
      ]),
      group(t("meas.reports"), t("meas.reports.help"), [
        reportModeRow(),
        switchCtl("overlay", "ReportHardware", t("meas.hardware"), t("meas.hardware.help")),
        row(t("meas.folder"), folder, h("div", { style: { display: "flex", gap: "8px" } },
          h("button", { class: "btn", type: "button", onclick: () => send("openFolder", {}) }, icon("folder"), t("meas.openFolder")),
          h("button", { class: "btn primary", type: "button", onclick: () => go("reports") }, t("meas.open")))),
      ]),
    ];
    settingsLayout(page, form, null);
  }

  // --- Interface --------------------------------------------------------------------------------

  function renderInterfacePage(page) {
    refreshers = [];
    const d = state.desktop;
    const langSeg = segmented([{ value: "auto", label: t("lang.auto") }, { value: "fr", label: "Français" }, { value: "en", label: "English" }],
      d.Language || "auto", v => setValue("desktop", "Language", v), t("ui.language"));
    const custom = h("div", { class: "colors" }, colorPick("desktop", "UiBackground", t("color.bg")), colorPick("desktop", "UiText", t("color.text")), colorPick("desktop", "UiAccent", t("color.accent")));
    const o = state.overlay;
    const panelPal = () => (o.Theme === 6 ? customPalette(o.CustomBackground || "#0e1116", o.CustomText || "#f0f3f8", o.CustomAccent || "#96bef0") : PANEL_THEMES[o.Theme || 0]);
    const chips = themeChips("desktop", "UiTheme", UI_THEMES,
      v => (v === "panel" ? t("theme.panel") : v === "custom" ? t("theme.6") : STYLED_THEMES[v] ? t("theme." + v) : t("theme." + (UI_THEMES.indexOf(v) - 1))),
      v => (v === "panel" ? panelPal() : v === "custom" ? customPalette(d.UiBackground || "#0e1116", d.UiText || "#f0f3f8", d.UiAccent || "#96bef0")
        : STYLED_THEMES[v] ? STYLED_THEMES[v].palette : PANEL_THEMES[UI_THEMES.indexOf(v) - 1]));
    const fsBtn = h("button", { class: "btn", type: "button", onclick: toggleFullscreen }, icon(state.fullscreen ? "compress" : "expand"), t(state.fullscreen ? "ui.fullscreen.off" : "ui.fullscreen.on"));
    const form = [
      group(t("ui.language"), null, [row(t("ui.language.label"), t("ui.language.help"), langSeg)]),
      group(t("ui.theme"), t("ui.theme.help"), [
        row("", null, chips, { stack: true }),
        row(t("theme.6"), null, custom, { enabled: () => get("desktop", "UiTheme") === "custom" }),
        sliderRow("desktop", "UiScale", t("ui.text"), null, 85, 130, pct, { step: 5 }),
        switchCtl("desktop", "UiAnimations", t("ui.motion"), t("ui.motion.help")),
      ]),
      group(t("ui.window"), null, [
        row(t("ui.fullscreen"), t("ui.fullscreen.help"), fsBtn),
        switchCtl("desktop", "UiStartFullscreen", t("ui.startFull")),
      ]),
      group(t("ui.shortcuts"), null, [
        switchCtl("desktop", "ShortcutDesktop", t("ui.shortcut.desktop"), t("ui.shortcut.desktop.help")),
        switchCtl("desktop", "ShortcutStart", t("ui.shortcut.start"), t("ui.shortcut.start.help")),
      ]),
    ];
    settingsLayout(page, form, null);
  }

  // --- Configuration --------------------------------------------------------------------------------

  // Relevé fait par le programme (ConfigProbe.cs) ; constats et mise en forme dans config.js.
  function requestConfig() {
    state.configLoading = true;
    send("diagnose");
    if (currentSub() === "config") renderPage();
  }

  const FINDING_ICON = { Good: "good", Problem: "problem", Warning: "alert", Info: "info" };
  const LAYER_PILL = { on: "pill good", off: "pill", missing: "pill warn", unreadable: "pill warn", nolib: "pill bad" };

  // Bouton « Désactiver » / « Réactiver » d'une layer : le programme demande confirmation avant de désactiver.
  function layerButton(action, named) {
    if (action.remove)
      return h("button", { class: "btn small", type: "button", onclick: () => send("removeXrLayer", { id: action.id, manifest: action.manifest, label: action.label }) },
        t("cfg.layer.remove"));
    const text = action.on ? t("cfg.layer.enable") : named ? t("cfg.layer.disableNamed", { name: action.label }) : t("cfg.layer.disable");
    return h("button", { class: "btn small" + (action.on ? "" : " danger"), type: "button",
      onclick: () => send("setXrLayer", { id: action.id, manifest: action.manifest, on: action.on, label: action.label }) }, text);
  }

  // Ordre des layers : un changement à la fois, puis le relevé est refait (message "config").
  function xrChange(type, payload) {
    if (state.xrBusy) return;
    state.xrBusy = true;
    document.querySelectorAll(".cfg-move button, .cfg-order button").forEach(b => { b.disabled = true; });
    send(type, payload);
  }

  function moveButtons(move, label) {
    const b = (delta, name, allowed) => h("button", { class: "btn icon small", type: "button", title: t(name), "aria-label": t(name) + " : " + label,
      disabled: !allowed || !!state.xrBusy, onclick: () => xrChange("moveXrLayer", { id: move.id, manifest: move.manifest, delta }) }, icon(delta < 0 ? "up" : "down"));
    return h("span", { class: "cfg-move" }, b(-1, "cfg.layer.up", move.up), b(1, "cfg.layer.down", move.down));
  }

  function renderConfigPage(page) {
    refreshers = [];
    const cfg = state.config;
    const refresh = h("button", { class: "btn", type: "button", disabled: state.configLoading, onclick: requestConfig }, icon("refresh"), t("cfg.refresh"));
    if (cfg === undefined) {
      settingsLayout(page, [group(t("cfg.findings"), null, [h("p", { class: "cfg-wait", text: t("cfg.loading") })])], null);
      return;
    }
    const findings = ConfigCheck.analyze(cfg, t);
    const problems = findings.filter(f => f.level === "Problem").length, warnings = findings.filter(f => f.level === "Warning").length;
    const count = (key, n) => t(key + (n === 1 ? ".one" : ".other"), { n });
    const verdict = problems ? h("span", { class: "pill bad" }, h("i", { class: "dot" }), count("cfg.count.problem", problems) + (warnings ? " · " + count("cfg.count.warning", warnings) : ""))
      : warnings ? h("span", { class: "pill warn" }, h("i", { class: "dot" }), count("cfg.count.warning", warnings))
      : h("span", { class: "pill good" }, h("i", { class: "dot" }), t("cfg.count.ok"));
    const when = cfg && cfg.collectedAt ? formatDate(cfg.collectedAt) : "";
    const copy = h("button", { class: "btn primary", type: "button", title: t("cfg.copy.help"), disabled: !cfg,
      onclick: () => send("copyText", { text: ConfigCheck.text(cfg, t, state.version) }) }, icon("copy"), t("cfg.copy"));

    const form = [group(t("cfg.findings"), null, [
      h("div", { class: "cfg-bar" }, verdict, h("span", { class: "muted small", text: state.configLoading ? t("cfg.loading") : when ? t("cfg.collected", { time: when }) : "" }),
        h("span", { class: "cfg-gap" }), refresh, copy),
      h("ul", { class: "findings cfg-findings" }, findings.map(f => h("li", { class: f.level }, icon(FINDING_ICON[f.level] || "info"),
        h("div", { class: "cfg-finding" }, h("span", { text: f.text }), f.action ? layerButton(f.action, true) : null)))),
      h("p", { class: "note", text: t("cfg.readOnly") }),
    ])];

    if (cfg) {
      for (const s of ConfigCheck.sections(cfg, t, num)) {
        const rows = s.rows.map(r => {
          const control = r.status
            ? h("div", { class: "cfg-tags" }, (r.tags || []).map(tag => h("span", { class: "cfg-tag", text: tag })), h("span", { class: LAYER_PILL[r.status] || "pill" }, h("i", { class: "dot" }), r.value),
                r.toggle ? layerButton(r.toggle, false) : null, r.move ? moveButtons(r.move, r.label) : null)
            : h("div", { class: "cfg-value", text: r.value || "--" });
          return row(r.label, r.sub || null, control);
        });
        if (s.id === "layers" && !rows.length) rows.push(h("p", { class: "note", text: t("cfg.layers.none") }));
        if (s.id === "layers" && rows.length) rows.push(h("div", { class: "note cfg-order" }, h("span", { text: t("cfg.layers.order") }),
          state.xrUndo ? h("button", { class: "btn small", type: "button", disabled: !!state.xrBusy, onclick: () => xrChange("undoXrLayers", {}) }, t("cfg.layers.undo")) : null));
        form.push(group(s.title, s.help || null, rows));
      }
    }
    settingsLayout(page, form, null);
    page.lastChild.classList.add("cfg");
  }

  // --- Rapports -------------------------------------------------------------------------------

  function levelClass(level) {
    return level === "Good" ? "good" : level === "Warning" ? "warn" : level === "Problem" ? "bad" : "";
  }

  function parseDate(iso) {
    if (!iso) return null;
    const d = new Date(iso);
    return isNaN(d) ? null : d;
  }

  function formatDate(iso, long) {
    const d = parseDate(iso);
    if (!d) return "";
    const locale = state.lang === "en" ? "en-GB" : "fr-FR";
    if (long) return new Intl.DateTimeFormat(locale, { weekday: "long", day: "numeric", month: "long", year: "numeric", hour: "2-digit", minute: "2-digit" }).format(d);
    return new Intl.DateTimeFormat(locale, { hour: "2-digit", minute: "2-digit" }).format(d);
  }

  function dayLabel(iso) {
    const d = parseDate(iso);
    if (!d) return "";
    const today = new Date();
    const start = x => new Date(x.getFullYear(), x.getMonth(), x.getDate()).getTime();
    const diff = Math.round((start(today) - start(d)) / 86400000);
    if (diff === 0) return t("rep.today");
    if (diff === 1) return t("rep.yesterday");
    return new Intl.DateTimeFormat(state.lang === "en" ? "en-GB" : "fr-FR", { weekday: "long", day: "numeric", month: "long", year: d.getFullYear() === today.getFullYear() ? undefined : "numeric" }).format(d);
  }

  function visibleReports() {
    const q = state.search.trim().toLowerCase();
    return state.reports.filter(r => (!state.gameFilter || r.game.toLowerCase() === state.gameFilter.toLowerCase()) && (!q || r.game.toLowerCase().includes(q)));
  }

  function selectReports(ids) {
    state.selected = ids;
    renderReportList();
    send("openReport", { ids });
  }

  function renderReportsPage(page) {
    page.className = "page flush";
    const games = Array.from(new Set(state.reports.map(r => r.game))).sort((a, b) => a.localeCompare(b));
    const filter = h("select", { class: "select", "aria-label": t("rep.allGames") }, h("option", { value: "", text: t("rep.allGames") }), games.map(g => h("option", { value: g, text: g })));
    filter.value = state.gameFilter;
    filter.addEventListener("change", () => { state.gameFilter = filter.value; renderReportList(); });
    const search = h("input", { class: "search", type: "search", placeholder: t("rep.search"), "aria-label": t("rep.search"), value: state.search });
    search.addEventListener("input", () => { state.search = search.value; renderReportList(); });
    const list = h("aside", { class: "rlist" },
      h("div", { class: "rlist-head" }, filter, search),
      h("div", { class: "rlist-items", id: "rlist-items", role: "listbox", "aria-multiselectable": "true", tabindex: 0 }),
      h("div", { class: "rlist-foot", id: "rlist-foot" }));
    list.querySelector("#rlist-items").addEventListener("keydown", onListKey);
    const view = h("section", { class: "rview", id: "rview" });
    page.appendChild(h("div", { class: "reports" }, list, view));
    renderReportList();
    if (state.selected.length && (!state.report || state.report.id !== state.selected[0])) send("openReport", { ids: state.selected });
    else if (!state.selected.length && visibleReports().length) selectReports([visibleReports()[0].id]);
    renderReportView(false);
  }

  function onListKey(e) {
    if (e.key === "Delete") { e.preventDefault(); askDelete(); return; }
    if (e.key !== "ArrowDown" && e.key !== "ArrowUp") return;
    e.preventDefault();
    const items = visibleReports();
    if (!items.length) return;
    let i = items.findIndex(r => r.id === state.selected[0]);
    i = Math.max(0, Math.min(items.length - 1, i + (e.key === "ArrowDown" ? 1 : -1)));
    selectReports([items[i].id]);
    const el = document.querySelector(`.ritem[data-id="${CSS.escape(items[i].id)}"]`);
    if (el) el.scrollIntoView({ block: "nearest" });
  }

  function renderReportList() {
    const box = document.getElementById("rlist-items");
    if (!box) return;
    box.replaceChildren();
    let day = null;
    const items = visibleReports();
    for (const r of items) {
      const label = dayLabel(r.start);
      if (label !== day) { day = label; box.appendChild(h("div", { class: "rday", text: label })); }
      const selected = state.selected.includes(r.id);
      const item = h("button", { type: "button", role: "option", class: "ritem" + (r.finished ? "" : " unfinished"), "aria-selected": String(selected), "data-id": r.id,
        title: r.finished ? null : t("rep.unfinished"),
        onclick: e => {
          if (e.ctrlKey || e.metaKey) {
            let ids = state.selected.slice();
            if (ids.includes(r.id)) ids = ids.filter(x => x !== r.id);
            else ids.push(r.id);
            if (!ids.length) ids = [r.id];
            selectReports(ids.slice(-2));
          } else selectReports([r.id]);
        } },
        h("i", { class: "lvl " + (r.level || "") }),
        h("span", { class: "g", text: r.game }),
        h("span", { class: "f num", text: r.fpsAvg == null ? "--" : num(r.fpsAvg, 1) }),
        h("span", { class: "d", text: formatDate(r.start) + (r.duration ? " · " + r.duration : "") + (r.manual ? " · " + t("rep.manualShort") : "") }),
        h("span", { class: "h", text: r.targetHeld == null ? "" : num(r.targetHeld) + " % " + t("rep.held") }));
      box.appendChild(item);
    }
    const foot = document.getElementById("rlist-foot");
    if (foot) foot.replaceChildren(h("span", { text: t("rep.count", { n: items.length }) }), h("span", { text: "· " + t("rep.compareHint") }));
  }

  function askDelete() {
    const ids = state.selected.slice();
    if (!ids.length) return;
    send("deleteReports", { ids, question: ids.length === 1 ? t("rep.delete.one") : t("rep.delete.many", { n: ids.length }) });
  }

  function reportTools(r) {
    const mode = segmented([{ value: "simple", label: t("rep.simple") }, { value: "detailed", label: t("rep.detailed") }, { value: "compare", label: t("rep.compare") }, { value: "text", label: t("rep.text") }],
      state.reportView === "analysis" ? state.readMode : state.reportView,
      v => {
        if (v === "simple" || v === "detailed") { state.readMode = v; state.reportView = "analysis"; send("setReadMode", { mode: v }); }
        else state.reportView = v;
        renderReportView(true);
      });
    return h("div", { class: "rtools" }, mode,
      h("button", { class: "btn ghost", type: "button", title: t("rep.notepad"), onclick: () => send("openNotepad", { ids: state.selected }) }, icon("text"), t("rep.notepad")),
      h("button", { class: "btn ghost", type: "button", title: t("rep.folder"), onclick: () => send("openFolder", { id: r.id }) }, icon("folder"), t("rep.folder")),
      h("button", { class: "btn ghost danger", type: "button", title: t("rep.delete"), onclick: askDelete }, icon("trash"), t("rep.delete")));
  }

  function renderReportView(animate) {
    const view = document.getElementById("rview");
    if (!view) return;
    Charts.hideTip();
    view.replaceChildren();
    if (!state.reports.length) {
      view.appendChild(h("div", { class: "empty" }, icon("reports"), h("h2", { text: t("rep.none.title") }), h("p", { text: t("rep.none") })));
      return;
    }
    const r = state.report;
    if (!r || !state.selected.includes(r.id)) {
      view.appendChild(h("div", { class: "empty" }, h("p", { text: t("rep.select") })));
      return;
    }
    const head = h("div", { class: "rhead" },
      h("div", { class: "title" },
        h("h2", { text: r.game }),
        h("div", { class: "when", text: formatDate(r.start, true) + (r.duration ? " · " + r.duration : "") + (r.manual ? " · " + t("rep.manual") : "") + (r.finished ? "" : " · " + t("rep.unfinished")) }),
        h("div", { class: "chips" }, (r.chips || []).map(c => h("span", { class: "chip", text: c })))),
      reportTools(r));
    view.appendChild(head);

    if (state.reportView === "text") { view.appendChild(h("pre", { class: "raw", text: r.raw || "" })); return; }
    if (state.reportView === "compare") { renderCompare(view, animate); return; }

    const analysis = h("div", { class: "analysis" });
    if (state.readMode === "simple") renderSimple(analysis, r, animate);
    else renderDetailed(analysis, r);
    view.appendChild(h("div", { class: "rbody" }, analysis, r.readable ? h("div", { class: "charts" }, reportCharts(r, animate)) : null));
  }

  // Lecture simple : verdict, puis chaque explication illustrée.
  function renderSimple(box, r, animate) {
    const s = r.simple || {};
    const n = r.n || {};
    const lvl = levelClass(s.level);
    const held = n.targetHeld;
    const judged = r.readable && held != null;
    box.appendChild(h("div", { class: "verdict", style: judged ? null : { gridTemplateColumns: "1fr" } },
      !judged ? null : Charts.ring(held, lvl ? `var(--${lvl})` : "var(--accent)", held == null ? "--" : num(held) + " %", n.target ? t("rep.target", { n: num(n.target) }) + " " + t("rep.held") : ""),
      h("div", null, h("h3", { text: s.verdict || "" }), h("p", { text: s.summary || "" }))));

    for (const sec of s.sections || []) {
      const el = h("section", { class: "sec" }, h("h3", { text: sec.title }));
      const fig = figureFor(sec.kind, r, animate);
      if (fig) el.appendChild(fig);
      for (const line of sec.lines || []) el.appendChild(h("p", { class: "line", text: line }));
      box.appendChild(el);
    }
    if (s.tips && s.tips.length) {
      box.appendChild(h("section", { class: "sec" }, h("h3", { text: t("rep.tryTitle") }),
        h("ul", { class: "tips" }, s.tips.map(tip => h("li", null, icon("tip"), h("span", { text: tip }))))));
    }
    if (judged && s.glossary && s.glossary.length) {
      box.appendChild(h("details", { class: "glossary" }, h("summary", { text: t("rep.glossary") }),
        h("dl", null, s.glossary.map(g => [h("dt", { text: g[0] }), h("dd", { text: g[1] })]))));
    }
    if (judged) box.appendChild(h("p", { class: "disclaimer", text: t("rep.simpleNote") }));
  }

  // Les 100 images : parts arrondies au plus juste (somme de 100), au moins une case pour une part non nulle.
  function framesFigure(dist, animate, heading) {
    if (!dist || dist.length !== 4 || dist[0] == null) return null;
    const shares = [dist[0] || 0, (dist[1] || 0) + (dist[2] || 0), dist[3] || 0];
    const cells = shares.map(Math.floor);
    shares.map((v, i) => [v - Math.floor(v), i]).sort((x, y) => y[0] - x[0]).slice(0, Math.max(0, 100 - cells.reduce((x, y) => x + y, 0))).forEach(([, i]) => cells[i]++);
    for (const i of [2, 1]) if (cells[i] === 0 && shares[i] >= 0.05) { cells[i] = 1; cells[0] = Math.max(0, cells[0] - 1); }
    const [ontime, late] = cells;
    const share = v => num(v, v > 0 && v < 1 ? 1 : 0) + " %";
    const grid = h("div", { class: "frames" + (animate ? " reveal" : ""), role: "img",
      "aria-label": `${heading ? heading + " : " : ""}${t("rep.frames.ontime")} ${share(shares[0])}, ${t("rep.frames.late")} ${share(shares[1])}, ${t("rep.frames.heavy")} ${share(shares[2])}` });
    for (let i = 0; i < 100; i++) {
      const cell = h("i", { class: i < ontime ? "" : i < ontime + late ? "late" : "heavy" });
      cell.style.setProperty("--i", i);
      grid.appendChild(cell);
    }
    return h("div", { class: "figure" }, heading ? h("div", { class: "row-label", style: { marginBottom: "10px" }, text: heading }) : null, grid,
      h("div", { class: "figure-cap" },
        h("span", { class: "k" }, h("i", { style: { background: "var(--good)" } }), `${share(shares[0])} ${t("rep.frames.ontime")}`),
        h("span", { class: "k" }, h("i", { style: { background: "var(--warn)" } }), `${share(shares[1])} ${t("rep.frames.late")}`),
        h("span", { class: "k" }, h("i", { style: { background: "var(--bad)" } }), `${share(shares[2])} ${t("rep.frames.heavy")}`)));
  }

  // Illustration de chaque partie de la lecture simple.
  function figureFor(kind, r, animate) {
    const n = r.n || {};
    const dist = n.distribution || [];
    if (kind === "felt") return framesFigure(dist, animate);
    if (kind === "limit") {
      const budget = n.budgetMs, lim = (r.simple || {}).limiter;
      // Valeurs jugées (minutes plafonnées par le jeu mises de côté) ; rapports d'avant la 1.7.1 : moyennes de la session.
      const cpuMs = "limitCpuMs" in n ? n.limitCpuMs : n.cpuMsAvg, gpuLoad = "limitGpuLoad" in n ? n.limitGpuLoad : n.gpuLoadAvg;
      const rows = [];
      if (cpuMs != null && budget) {
        const share = Math.min(1.25, cpuMs / budget);
        rows.push(barRow(t("rep.limit.cpu"), t("rep.limit.cpuSub"), share / 1.25 * 100, (1 / 1.25) * 100, share >= 0.85 ? "warn" : "", lim === "cpu" || lim === "both",
          num(cpuMs, 1) + " " + t("unit.ms"), t("rep.limit.available", { ms: num(budget, 1) })));
      }
      if (gpuLoad != null) {
        rows.push(barRow(t("rep.limit.gpu"), t("rep.limit.gpuSub"), gpuLoad, null, gpuLoad >= 90 ? "warn" : "", lim === "gpu" || lim === "both",
          num(gpuLoad) + " %", n.gpuLoadMax != null ? t("rep.health.max") + " " + num(n.gpuLoadMax) + " %" : ""));
      }
      return rows.length ? h("div", { class: "figure" }, h("div", { class: "bars" }, rows)) : null;
    }
    if (kind === "health") {
      const rows = [];
      const temp = (label, value, warnAt) => {
        if (value == null) return;
        const lo = 30, hi = 100;
        const p = ((Math.min(hi, Math.max(lo, value)) - lo) / (hi - lo)) * 100;
        const zone = ((hi - warnAt) / (hi - lo)) * 100;
        rows.push(barRow(label, t("rep.health.max"), p, null, value >= warnAt ? "bad" : value >= warnAt - 8 ? "warn" : "good", false, num(value) + " °C", "", zone));
      };
      temp(t("rep.health.cpu"), n.cpuTempMax, 90);
      temp(t("rep.health.gpu"), n.gpuTempMax, 85);
      if (n.vramMax != null && n.vramTotal) {
        const p = (100 * n.vramMax) / n.vramTotal;
        rows.push(barRow(t("rep.health.vram"), t("rep.health.max"), p, null, p >= 95 ? "bad" : p >= 85 ? "warn" : "", false,
          num(n.vramMax, 1) + " " + t("unit.gb"), t("rep.on", { v: num(n.vramTotal, 1) + " " + t("unit.gb") })));
      }
      const fig = rows.length ? h("div", { class: "figure" }, h("div", { class: "bars" }, rows)) : null;
      return fig;
    }
    if (kind === "compare") {
      const p = (r.simple || {}).previous;
      if (!p) return null;
      const pair = (title, before, now, max, unit, digits) => {
        const w = v => (v == null || !max ? 0 : Math.max(2, Math.min(100, (100 * v) / max)));
        return h("div", { class: "cbar" }, h("div", { class: "row-label", text: title }),
          h("div", { class: "cb-row" }, h("span", { class: "muted", text: t("rep.cmp.before") }), h("div", { class: "bar-track" }, h("i", { style: { width: w(before) + "%", background: "var(--muted)" } })), h("b", { text: num(before, digits) + unit })),
          h("div", { class: "cb-row" }, h("span", { text: t("rep.cmp.now") }), h("div", { class: "bar-track" }, h("i", { style: { width: w(now) + "%" } })), h("b", { text: num(now, digits) + unit })));
      };
      const fpsMax = Math.max(n.target || 0, n.fpsAvg || 0, p.fpsAvg || 0);
      return h("div", { class: "figure" }, h("div", { class: "compare-bars" },
        pair(t("rep.cmp.fps"), p.fpsAvg, n.fpsAvg, fpsMax, "", 1),
        pair(t("rep.cmp.held"), p.targetHeld, n.targetHeld, 100, " %", 0)));
    }
    return null;
  }

  function barRow(label, sub, percent, mark, cls, limiting, value, note, zone) {
    const track = h("div", { class: "bar-track" });
    if (zone) track.appendChild(h("span", { class: "zone", style: { width: zone + "%" } }));
    track.appendChild(h("i", { class: cls || "", style: { width: Math.max(1, Math.min(100, percent)) + "%" } }));
    if (mark != null) track.appendChild(h("span", { class: "mark", style: { left: "calc(" + mark + "% - 1px)" } }));
    return h("div", { class: "bar-row" },
      h("div", { class: "lab" }, label, limiting ? h("span", { class: "tag-limit", text: t("rep.limit.tag") }) : null, h("small", { text: sub })),
      track,
      h("div", { class: "bar-val" }, h("b", { text: value }), h("span", { text: note || "" })));
  }

  // Lecture détaillée : constats et tous les chiffres.
  function renderDetailed(box, r) {
    const n = r.n || {};
    box.appendChild(h("section", { class: "sec", style: { marginTop: "0" } }, h("h3", { text: t("rep.findings") }),
      h("ul", { class: "findings" }, (r.findings || []).map(f => h("li", { class: f.level }, icon(f.level === "Good" ? "good" : f.level === "Problem" ? "problem" : f.level === "Warning" ? "alert" : "info"), h("span", { text: f.text }))))));
    if (!r.readable) return;
    const stat = (label, value, unit, sub) => h("div", { class: "stat" }, h("div", { class: "l", text: label }), h("div", { class: "v" }, value, unit ? h("small", { text: unit }) : null), sub ? h("div", { class: "s", text: sub }) : null);
    box.appendChild(h("section", { class: "sec" }, h("h3", { text: t("rep.fpsSec") }), h("div", { class: "stats" },
      stat(t("rep.avg"), num(n.fpsAvg, 1), t("unit.fps"), n.target ? t("rep.target", { n: num(n.target) }) + (n.targetHeld != null ? ", " + num(n.targetHeld) + " % " + t("rep.held") : "") : null),
      stat(t("rep.min") + " / " + t("rep.max"), num(n.fpsMin) + " / " + num(n.fpsMax), t("unit.fps")),
      stat(t("rep.low1"), num(n.low1, 1), t("unit.fps")),
      stat(t("rep.low01"), num(n.low01, 1), t("unit.fps")),
      n.outFps != null ? stat(t("rep.fg"), num(n.outFps, 1), t("unit.fps")) : null)));

    const gpuLabel = n.gpuIsMeasured ? t("rep.gpuMeasured") : t("rep.gpuEstimated");
    box.appendChild(h("section", { class: "sec" }, h("h3", { text: t("rep.frameSec") + " (" + t("unit.ms") + ")" }),
      h("table", { class: "grid" },
        h("thead", null, h("tr", null, h("th", { text: "" }), h("th", { text: t("rep.avg") }), h("th", { text: t("rep.max") }))),
        h("tbody", null,
          h("tr", null, h("td", { text: t("rep.frame") }), h("td", { text: num(n.frameAvg, 1) }), h("td", { text: num(n.frameMax, 1) })),
          h("tr", null, h("td", { text: t("rep.cpu") }), h("td", { text: num(n.cpuMsAvg, 1) }), h("td", { text: num(n.cpuMsMax, 1) })),
          h("tr", null, h("td", { text: gpuLabel }), h("td", { text: num(n.gpuMsAvg, 1) }), h("td", { text: num(n.gpuMsMax, 1) })),
          n.gpuIsMeasured ? h("tr", null, h("td", { text: t("rep.gpuEstimated") }), h("td", { text: num(n.gpuEstimateAvg, 1) }), h("td", { text: num(n.gpuEstimateMax, 1) })) : null))));

    const dist = n.distribution || [];
    if (dist.length === 4 && dist[0] != null) {
      const colors = ["var(--good)", "var(--warn)", "color-mix(in srgb, var(--warn) 50%, var(--bad))", "var(--bad)"];
      box.appendChild(h("section", { class: "sec" }, h("h3", { text: t("rep.distSec") }),
        h("p", { class: "muted small", text: t("rep.budgetOf", { ms: num(n.budgetMs, 1) }) }),
        h("div", { class: "dist", role: "img", "aria-label": t("rep.distSec") }, dist.map((v, i) => (v > 0 ? h("i", { style: { flex: String(v), background: colors[i] }, title: `${t("rep.dist." + i)} : ${num(v, 1)} %` }) : null))),
        h("div", { class: "figure-cap" }, dist.map((v, i) => h("span", { class: "k" }, h("i", { style: { background: colors[i] } }), `${t("rep.dist." + i)} ${num(v, 1)} %`)))));
    }

    box.appendChild(h("section", { class: "sec" }, h("h3", { text: t("rep.hwSec") }),
      h("table", { class: "grid" },
        h("thead", null, h("tr", null, h("th", { text: "" }), h("th", { text: t("rep.cpu") + " " + t("rep.avg") }), h("th", { text: t("rep.max") }), h("th", { text: t("rep.gpu") + " " + t("rep.avg") }), h("th", { text: t("rep.max") }))),
        h("tbody", null,
          h("tr", null, h("td", { text: t("rep.loadPct") }), h("td", { text: num(n.cpuLoadAvg) }), h("td", { text: num(n.cpuLoadMax) }), h("td", { text: num(n.gpuLoadAvg) }), h("td", { text: num(n.gpuLoadMax) })),
          h("tr", null, h("td", { text: t("rep.tempC") }), h("td", { text: num(n.cpuTempAvg) }), h("td", { text: num(n.cpuTempMax) }), h("td", { text: num(n.gpuTempAvg) }), h("td", { text: num(n.gpuTempMax) })),
          h("tr", null, h("td", { text: t("rep.freqMHz") }), h("td", { text: num(n.cpuMHzAvg) }), h("td", { text: "" }), h("td", { text: num(n.gpuMHzAvg) }), h("td", { text: "" })),
          h("tr", null, h("td", { text: t("rep.vramGb") }), h("td", { text: "" }), h("td", { text: "" }), h("td", { text: num(n.vramAvg, 1) }), h("td", { text: num(n.vramMax, 1) + (n.vramTotal ? " / " + num(n.vramTotal, 1) : "") }))))));
    box.appendChild(h("p", { class: "disclaimer", text: t("rep.detailNote") }));
  }

  // Courbes du relevé minute par minute, à côté de l'analyse.
  function reportCharts(r, animate) {
    const m = r.minutes;
    const n = r.n || {};
    if (!m || !m.minute || m.minute.length < 2) return [h("div", { class: "chart" }, h("div", { class: "chart-empty", text: t("chart.none") }))];
    const minuteLabel = v => num(v) + " " + t("unit.min");
    const tipTitle = v => t("chart.minute", { m: v });
    const common = { labels: m.minute, xLabel: minuteLabel, tipTitle, animate: animate && !document.documentElement.classList.contains("no-anim") };
    const gpuName = r.gpuImplausible ? t("chart.gpuRemoved") : r.minutesGpuMeasured ? t("rep.gpuMeasured") : t("rep.gpuEstimated");
    return [
      Charts.line(Object.assign({}, common, { title: t("chart.fps"), digits: 1, min: 0,
        series: [{ name: t("chart.fps.avg"), color: cssVar("--s-fps"), values: m.fpsAvg }, { name: t("chart.fps.min"), color: cssVar("--s-fps-2"), values: m.fpsMin }],
        refs: n.target ? [{ value: n.target, label: t("chart.target") + " " + num(n.target) }] : [] })),
      Charts.line(Object.assign({}, common, { title: t("chart.frame"), unit: t("unit.ms"), digits: 1, min: 0,
        series: [{ name: t("rep.cpu"), color: cssVar("--s-cpu"), values: m.cpuMs },
          { name: gpuName, color: cssVar("--s-gpu"), values: r.gpuImplausible ? m.gpuMs.map(() => null) : m.gpuMs, dashed: !r.minutesGpuMeasured, legendOnly: r.gpuImplausible }],
        refs: n.budgetMs ? [{ value: n.budgetMs, label: t("chart.budget") + " " + num(n.budgetMs, 1) }] : [], forceLegend: true })),
      Charts.line(Object.assign({}, common, { title: t("chart.load"), unit: "%", digits: 0, min: 0, max: 100, height: 120,
        series: [{ name: t("rep.cpu"), color: cssVar("--s-cpu"), values: m.cpuLoad }, { name: t("rep.gpu"), color: cssVar("--s-gpu"), values: m.gpuLoad }] })),
      Charts.line(Object.assign({}, common, { title: t("chart.temp"), unit: "°C", digits: 0, height: 120,
        series: [{ name: t("rep.cpu"), color: cssVar("--s-cpu"), values: m.cpuTemp }, { name: t("rep.gpu"), color: cssVar("--s-gpu"), values: m.gpuTemp }] })),
      Charts.line(Object.assign({}, common, { title: t("chart.vram"), unit: t("unit.gb"), digits: 1, min: 0, height: 100,
        series: [{ name: t("chart.vram"), color: cssVar("--s-vram"), values: m.vram }],
        refs: n.vramTotal ? [{ value: n.vramTotal, label: t("chart.total") + " " + num(n.vramTotal, 1) }] : [] })),
    ];
  }

  // Comparaison : tableau des écarts et les deux courbes de FPS.
  function renderCompare(view, animate) {
    const a = state.report, b = state.other;
    if (!b) { view.appendChild(h("div", { class: "empty" }, icon("compare"), h("h2", { text: t("rep.cmp.title") }), h("p", { text: t("rep.cmp.pick") }))); return; }
    const short = r => formatDate(r.start, false) + " " + new Intl.DateTimeFormat(state.lang === "en" ? "en-GB" : "fr-FR", { day: "numeric", month: "short" }).format(parseDate(r.start) || new Date());
    const nameA = "A · " + short(a), nameB = "B · " + short(b);
    const colorA = cssVar("--s-fps"), colorB = cssVar("--s-fps-2");
    const notes = [state.otherChosen ? t("rep.cmp.chosen") : t("rep.cmp.prev")];
    if (state.compareNote.setup) notes.push(t("rep.cmp.setup", { v: state.compareNote.setup }));
    if (state.compareNote.hardware) notes.push(t("rep.cmp.hw", { v: state.compareNote.hardware }));
    view.appendChild(h("p", { class: "muted", style: { margin: "0 0 14px", maxWidth: "90ch" }, text: notes.join("  ") }));
    const na = a.n || {}, nb = b.n || {};

    // Chiffres clés : valeurs de A et B, puis une barre d'écart verte (A fait mieux) ou rouge (A fait moins bien).
    const rows = [
      ["rep.c.fpsAvg", "fpsAvg", 1, true], ["rep.c.fpsMin", "fpsMin", 0, true], ["rep.low1", "low1", 1, true], ["rep.low01", "low01", 1, true],
      ["rep.c.held", "targetHeld", 0, true], ["rep.c.fg", "outFps", 1, true], ["rep.c.frame", "frameAvg", 1, false], ["rep.c.cpu", "cpuMsAvg", 1, false],
      ["rep.c.gpu", "gpuMsAvg", 1, false], ["rep.c.cpuLoad", "cpuLoadAvg", 0, null], ["rep.c.gpuLoad", "gpuLoadAvg", 0, null],
      ["rep.c.cpuTemp", "cpuTempMax", 0, false], ["rep.c.gpuTemp", "gpuTempMax", 0, false], ["rep.c.vram", "vramMax", 1, null],
    ];
    const tbody = h("tbody");
    for (const [key, field, digits, up] of rows) {
      const va = na[field], vb = nb[field];
      if (va == null && vb == null) continue;
      const d = va != null && vb != null ? va - vb : null;
      const flat = d == null || Math.abs(d) < 1e-9;
      // Charges et mémoire : ni mieux ni moins bien en soi (une carte plus chargée peut simplement travailler davantage).
      const better = !flat && up != null && (up ? d > 0 : d < 0);
      const cls = flat || up == null ? "" : better ? "up" : "down";
      // Longueur : écart relatif à B, 25 % d'écart remplissent la demi-barre.
      const rel = flat || !vb ? 0 : Math.min(1, Math.abs(d / vb) / 0.25);
      const bar = h("div", { class: "delta-bar", "aria-hidden": "true" }, h("span", { class: "axis" }),
        flat ? null : h("i", { class: up == null ? "neutral" : better ? "good" : "bad", style: d > 0 ? { left: "50%", width: Math.max(1.5, rel * 50) + "%" } : { right: "50%", width: Math.max(1.5, rel * 50) + "%" } }));
      tbody.appendChild(h("tr", null, h("td", { text: t(key) }), h("td", { text: num(va, digits) }), h("td", { text: num(vb, digits) }),
        h("td", { class: "delta-cell" }, bar), h("td", { class: cls, text: d == null ? "--" : (d > 0 ? "+" : "") + num(d, digits) })));
    }
    const legend = h("div", { class: "legend", style: { margin: "0 0 6px" } },
      h("span", null, h("i", { style: { background: colorA } }), h("span", { text: nameA })), h("span", null, h("i", { style: { background: colorB } }), h("span", { text: nameB })));
    const table = h("table", { class: "grid compare" }, h("thead", null, h("tr", null, h("th", { text: "" }),
      h("th", null, h("span", { class: "key-a", style: { background: colorA } }), "A"), h("th", null, h("span", { class: "key-a", style: { background: colorB } }), "B"),
      h("th", { class: "delta-cell", text: t("rep.cmp.better") }), h("th", { text: t("rep.cmp.delta") }))), tbody);

    // Les 100 images de chaque session, côte à côte.
    const grids = h("div", { class: "frames-pair" }, framesFigure((a.n || {}).distribution, animate, nameA), framesFigure((b.n || {}).distribution, animate, nameB));

    // Courbes : A et B superposées, minute par minute.
    const ma = a.minutes, mb = b.minutes;
    const charts = [];
    if (ma && mb && ma.minute.length >= 2 && mb.minute.length >= 2) {
      const len = Math.max(ma.minute.length, mb.minute.length);
      const labels = Array.from({ length: len }, (_, i) => i + 1);
      const pad = v => labels.map((_, i) => (v && i < v.length ? v[i] : null));
      const common = { labels, xLabel: v => num(v) + " " + t("unit.min"), tipTitle: v => t("chart.minute", { m: v }), animate: animate && !document.documentElement.classList.contains("no-anim"), forceLegend: true };
      const pair = (field, extra) => Object.assign({}, common, extra, {
        series: [{ name: nameA, color: colorA, values: pad(ma[field]) }, { name: nameB, color: colorB, values: pad(mb[field]) }] });
      charts.push(Charts.line(pair("fpsAvg", { title: t("chart.fps"), digits: 1, min: 0, refs: na.target ? [{ value: na.target, label: t("chart.target") + " " + num(na.target) }] : [] })));
      charts.push(Charts.line(pair("cpuMs", { title: t("chart.cpuTime"), unit: t("unit.ms"), digits: 1, min: 0, height: 130, refs: na.budgetMs ? [{ value: na.budgetMs, label: t("chart.budget") + " " + num(na.budgetMs, 1) }] : [] })));
      charts.push(Charts.line(pair("gpuMs", { title: t("chart.gpuTime"), unit: t("unit.ms"), digits: 1, min: 0, height: 130, refs: na.budgetMs ? [{ value: na.budgetMs, label: t("chart.budget") + " " + num(na.budgetMs, 1) }] : [] })));
      charts.push(Charts.line(pair("gpuLoad", { title: t("chart.gpuLoad"), unit: "%", digits: 0, min: 0, max: 100, height: 110 })));
      charts.push(Charts.line(pair("gpuTemp", { title: t("chart.gpuTemp"), unit: "°C", digits: 0, height: 110 })));
      charts.push(Charts.line(pair("vram", { title: t("chart.vram"), unit: t("unit.gb"), digits: 1, min: 0, height: 110 })));
    } else {
      // Sans relevé minute par minute : les deux grilles prennent la place des courbes.
      view.appendChild(h("div", { class: "rbody" }, h("div", null, legend, table),
        h("div", { class: "charts" }, grids, h("div", { class: "chart" }, h("div", { class: "chart-empty", text: t("chart.cmpNone") })))));
      return;
    }
    view.appendChild(h("div", { class: "rbody" }, h("div", null, legend, table, grids), h("div", { class: "charts" }, charts)));
  }

  // --- Divers ------------------------------------------------------------------------------------

  let toastTimer = 0;
  // --- Fenêtres modales (aide-mémoire, bienvenue) ------------------------------------------------

  let modal = null;

  function openModal(title, body, opts) {
    closeModal();
    opts = opts || {};
    const box = h("div", { class: "modal" + (opts.wide ? " wide" : ""), role: "dialog", "aria-modal": "true", "aria-label": title },
      h("div", { class: "modal-head" }, h("h2", { text: title }),
        opts.noClose ? null : h("button", { class: "btn icon ghost", type: "button", "aria-label": t("modal.close"), onclick: () => closeModal() }, icon("close"))),
      h("div", { class: "modal-body" }, body),
      opts.foot ? h("div", { class: "modal-foot" }, opts.foot) : null);
    const back = h("div", { class: "modal-back" }, box);
    back.addEventListener("mousedown", e => { if (e.target === back && !opts.noClose) closeModal(); });
    document.body.appendChild(back);
    modal = { back, onClose: opts.onClose };
    const focus = box.querySelector("input, button.primary, button");
    if (focus) focus.focus();
    return box;
  }

  function closeModal() {
    if (!modal) return;
    const m = modal;
    modal = null;
    m.back.remove();
    if (m.onClose) m.onClose();
  }

  // --- Recherche d'un réglage (Ctrl+K) -----------------------------------------------------------

  // Pages (et onglets) où chercher : construites hors écran pour lire leurs réglages, dans la langue du moment.
  const SEARCH_TARGETS = [["panel", "panel"], ["panel", "appearance"], ["watch"], ["window"], ["image"],
    ["video", "rec"], ["video", "live"], ["video", "snap"], ["video", "sound"], ["video", "camera"], ["video", "obs"],
    ["desktop"], ["diagnostics", "measures"], ["hotkeys"], ["interface"]];
  const fold = text => (text || "").normalize("NFD").replace(/[̀-ͯ]/g, "").toLowerCase();

  function buildSearchIndex() {
    const saved = { refreshers, page: state.page, tab: Object.assign({}, state.tab) };
    const items = [];
    indexing = true;
    try {
      for (const [pageId, tab] of SEARCH_TARGETS) {
        state.page = pageId;
        if (tab) state.tab[pageId] = tab;
        const box = h("div");
        try { PAGE_BUILDERS[pageId](box); } catch (e) { continue; }
        const where = t("nav." + pageId) + (tab && TABS[pageId] ? " › " + t(TABS[pageId].find(x => x.id === tab).label) : "");
        items.push({ page: pageId, tab, label: where, where: "", page_only: true });
        box.querySelectorAll(".row").forEach(r => {
          const labelEl = r.querySelector(".row-label");
          const label = labelEl ? labelEl.firstChild && labelEl.firstChild.nodeType === 3 ? labelEl.firstChild.textContent : labelEl.textContent : "";
          if (!label || !label.trim()) return;
          const groupEl = r.closest(".group");
          const groupTitle = groupEl ? groupEl.querySelector(".group-head h2").textContent : "";
          const helpEl = r.querySelector(".row-help");
          items.push({ page: pageId, tab, label: label.trim(), where: where + (groupTitle && groupTitle !== label ? " › " + groupTitle : ""), help: helpEl ? helpEl.textContent : "" });
        });
      }
    } finally {
      indexing = false;
      refreshers = saved.refreshers;
      state.page = saved.page;
      state.tab = saved.tab;
    }
    return items;
  }

  let search = null;

  function openSearch() {
    if (search || hotkeyCapture) return;
    const items = buildSearchIndex();
    const input = h("input", { class: "search-input", type: "search", placeholder: t("search.placeholder"), "aria-label": t("search.open"), autocomplete: "off", spellcheck: "false" });
    const list = h("div", { class: "search-results", role: "listbox" });
    let results = [], active = 0;
    const show = () => {
      const q = fold(input.value.trim());
      const words = q.split(/\s+/).filter(Boolean);
      results = !words.length ? items.filter(x => x.page_only)
        : items.map(x => {
            const label = fold(x.label), all = label + " " + fold(x.where) + " " + fold(x.help);
            if (!words.every(w => all.includes(w))) return null;
            const score = (words.every(w => label.includes(w)) ? 0 : 2) + (label.startsWith(words[0]) ? 0 : 1) + (x.page_only ? 1 : 0);
            return { x, score };
          }).filter(Boolean).sort((a, b) => a.score - b.score).map(r => r.x);
      results = results.slice(0, 40);
      active = Math.min(active, Math.max(0, results.length - 1));
      list.replaceChildren(...(results.length ? results.map((x, i) => h("button", { type: "button", class: "search-item", role: "option", "aria-selected": String(i === active),
          onmousemove: () => { if (active !== i) { active = i; mark(); } }, onclick: () => pick(x) },
          h("span", { class: "search-label", text: x.label }), x.where ? h("span", { class: "search-where", text: x.where }) : null))
        : [h("p", { class: "muted", style: { padding: "14px 16px" }, text: t("search.none") })]));
    };
    const mark = () => list.querySelectorAll(".search-item").forEach((b, i) => {
      b.setAttribute("aria-selected", String(i === active));
      if (i === active) b.scrollIntoView({ block: "nearest" });
    });
    const pick = x => {
      closeSearch();
      go(x.page, x.tab);
      if (x.page_only) return;
      // Le réglage trouvé est amené à l'écran et signalé un instant.
      const target = Array.from(document.querySelectorAll("#page .row")).find(r => {
        const l = r.querySelector(".row-label");
        return l && (l.firstChild ? l.firstChild.textContent : l.textContent).trim() === x.label;
      });
      if (target) {
        target.scrollIntoView({ block: "center" });
        target.classList.add("found");
        setTimeout(() => target.classList.remove("found"), 1800);
        const control = target.querySelector("input, select, button");
        if (control) control.focus({ preventScroll: true });
      }
    };
    input.addEventListener("input", () => { active = 0; show(); });
    input.addEventListener("keydown", e => {
      if (e.key === "ArrowDown") { e.preventDefault(); active = Math.min(results.length - 1, active + 1); mark(); }
      else if (e.key === "ArrowUp") { e.preventDefault(); active = Math.max(0, active - 1); mark(); }
      else if (e.key === "Enter" && results[active]) { e.preventDefault(); pick(results[active]); }
      else if (e.key === "Escape") { e.preventDefault(); closeSearch(); }
    });
    const box = h("div", { class: "search-box", role: "dialog", "aria-modal": "true", "aria-label": t("search.open") },
      h("label", { class: "search-field" }, icon("search"), input, h("kbd", { text: t("search.esc") })), list,
      h("div", { class: "search-foot" }, h("span", null, h("kbd", { text: "↑ ↓" }), " " + t("search.move")), h("span", null, h("kbd", { text: t("search.enter") }), " " + t("search.go"))));
    const back = h("div", { class: "modal-back search-back" }, box);
    back.addEventListener("mousedown", e => { if (e.target === back) closeSearch(); });
    document.body.appendChild(back);
    search = back;
    show();
    input.focus();
  }

  function closeSearch() {
    if (!search) return;
    search.remove();
    search = null;
  }

  // --- Choix tout faits (mode simple, bienvenue) ----------------------------------------------------

  // Contenu du panneau : disposition et éléments affichés (page Apparence).
  const CONTENT_CHOICES = [
    { id: "minimal", values: { Layout: 2, Clock: false, HeadsetInfo: false, PanelSession: false, PanelBattery: false, PowerColumn: false } },
    { id: "essential", values: { Layout: 1, Clock: true, HeadsetInfo: false, PanelSession: true, PanelBattery: false, PowerColumn: false } },
    { id: "full", values: { Layout: 0, Clock: true, HeadsetInfo: true, PanelSession: true, PanelBattery: true, PowerColumn: true } },
  ];
  // Qualité de la vidéo : résolution et qualité (le codec et le débit suivent).
  const QUALITY_CHOICES = [
    { id: "standard", values: { VideoHeight: 1080, VideoQuality: "balanced" } },
    { id: "high", values: { VideoHeight: 1440, VideoQuality: "high" } },
    { id: "max", values: { VideoHeight: 2160, VideoQuality: "max" } },
  ];

  // Grandes cartes à choix unique ; la carte en cours suit les réglages (aucune : réglages personnels).
  function choiceCards(scope, choices, prefix, opts) {
    opts = opts || {};
    const box = h("div", { class: "choices", role: "group" });
    const matches = c => Object.keys(c.values).every(k => get(scope, k) === c.values[k]);
    const cards = choices.map(c => {
      const b = h("button", { type: "button", class: "choice", onclick: () => {
        for (const k of Object.keys(c.values)) if (get(scope, k) !== c.values[k]) setValue(scope, k, c.values[k]);
        refresh();
      } },
        h("span", { class: "choice-title" }, t(prefix + c.id), c.id === opts.recommended ? h("span", { class: "choice-tag", text: t("choice.recommended") }) : null),
        h("span", { class: "choice-help", text: t(prefix + c.id + ".help") }));
      b.disabled = locked(scope);
      box.appendChild(b);
      return b;
    });
    const refresh = () => cards.forEach((b, i) => b.setAttribute("aria-pressed", String(matches(choices[i]))));
    refreshers.push(refresh);
    refresh();
    return box;
  }

  // Lien vers la page avancée correspondante.
  function moreLink(text, pageId, tab) {
    return h("p", { class: "more-link" }, h("a", { href: "#", onclick: e => { e.preventDefault(); go(pageId, tab); } }, t(text), icon("chevron")));
  }

  // Preset d'image dont les réglages sont ceux du moment (null : réglages personnels).
  function currentPreset() {
    const presets = get("overlay", "ImagePresets") || [];
    return presets.find(p => Object.keys(p.values).every(k => get("overlay", k) === p.values[k])) || null;
  }
  const presetLabel = p => (p.builtin ? t("img.preset." + p.id) : p.name);

  // --- Mode simple ---------------------------------------------------------------------------------

  function renderHome(page) {
    refreshers = [];
    const dash = h("div", { class: "dash" });
    dashNotes(dash);
    dash.appendChild(h("section", { class: "card dash-stage" },
      h("div", { class: "card-head" }, h("h2", { text: t("dash.preview") }), h("span", { class: "muted small", id: "panel-caption" })),
      h("div", { class: "stage", id: "panel-stage" }, h("img", { id: "panel-img", alt: t("dash.preview") }), h("div", { class: "stage-empty", id: "panel-empty", hidden: true, text: t("dash.preview.none") }))));
    dash.appendChild(h("section", { class: "card" }, h("div", { class: "card-head" }, h("h2", { text: t("home.headset") }),
        h("a", { href: "#", class: "small", onclick: e => { e.preventDefault(); go("simple-headset"); }, text: t("home.customize") })),
      h("div", { class: "quick" },
        switchCtl("overlay", "Visible", t("home.panel"), t("home.panel.help", { key: hotkeyText(get("overlay", "Hotkey.toggle") || 0) || "--" })),
        switchCtl("overlay", "WatchEnabled", t("home.watch")),
        switchCtl("overlay", "WindowEnabled", t("home.window"), t("home.window.help")))));

    // Enregistrer et partager : état et bouton de la vidéo (page Vidéo), capture d'écran.
    const snap = h("button", { class: "btn", type: "button", disabled: !videoEnabled(), onclick: () => send("snapshot") }, icon("camera"), t("snap.take"));
    dash.appendChild(h("section", { class: "card span" }, h("div", { class: "card-head" }, h("h2", { text: t("home.share") }),
        h("a", { href: "#", class: "small", onclick: e => { e.preventDefault(); go("simple-video"); }, text: t("home.customize") })),
      videoEnabled()
        ? h("div", { class: "home-share" },
            segmented([{ value: "file", label: t("home.share.file") }, { value: "stream", label: t("home.share.live") }], get("desktop", "VideoTarget"),
              v => { setValue("desktop", "VideoTarget", v); updateVideo(); }, t("vid.target")),
            h("div", { class: "video-card", id: "video-card", "aria-live": "polite" }),
            h("div", { class: "row-buttons" }, snap,
              h("button", { class: "btn ghost", type: "button", onclick: () => send("videoOpenFolder") }, icon("folder"), t("home.share.folder"))))
        : h("div", { class: "row-buttons" }, h("span", { class: "muted", text: t("home.share.off") }),
            h("button", { class: "btn", type: "button", onclick: () => setValue("overlay", "VideoCapture", true) }, t("home.share.enable")))));

    // Capteurs en direct.
    dash.appendChild(h("section", { class: "card span" }, h("div", { class: "card-head" }, h("h2", { text: t("dash.sensors") })),
      h("div", { class: "tiles" }, h("div", { class: "tile", id: "tile-cpu" }), h("div", { class: "tile", id: "tile-gpu" }), h("div", { class: "tile", id: "tile-vram" }))));

    // Dernière session, puis le rendu de l'image (presets en un clic).
    dash.appendChild(lastSessionCard());
    dash.appendChild(h("section", { class: "card" }, h("div", { class: "card-head" }, h("h2", { text: t("home.image") }),
        h("a", { href: "#", class: "small", onclick: e => { e.preventDefault(); go("simple-image"); }, text: t("home.image.more") })),
      presetChips(6)));
    page.appendChild(dash);
    refreshers.push(updateVideo);
    updateVideo();
  }

  // Presets d'image en pastilles : « Aucun effet », puis les fournis (et les vôtres) ; `max` : nombre de fournis.
  function presetChips(max) {
    const presets = get("overlay", "ImagePresets") || [];
    const now = currentPreset();
    const off = (get("overlay", "ImageMode") || "off") === "off";
    const list = presets.filter(p => p.builtin).slice(0, max || 99).concat(max ? [] : presets.filter(p => !p.builtin));
    if (max && now && !list.includes(now)) list.push(now);
    const chip = (label, pressed, onclick) => {
      const b = h("button", { type: "button", class: "chip-btn", "aria-pressed": String(pressed), text: label, onclick });
      b.disabled = locked("overlay");
      return b;
    };
    return h("div", { class: "chip-row", role: "group", "aria-label": t("img.preset") },
      chip(t("home.image.none"), off, () => setValue("overlay", "ImageMode", "off")),
      list.map(p => chip(presetLabel(p), !off && now === p, () => send("imagePresetApply", { id: p.id }))));
  }

  function renderSimpleHeadset(page) {
    refreshers = [];
    const o = state.overlay;
    const form = [
      group(t("s.perf"), t("s.perf.help"), [
        switchCtl("overlay", "Visible", t("s.perf.show"), t("home.panel.help", { key: hotkeyText(get("overlay", "Hotkey.toggle") || 0) || "--" })),
        row(t("s.content"), null, choiceCards("overlay", CONTENT_CHOICES, "content.", { recommended: "essential" }), { stack: true }),
        segRow("overlay", "Anchor", t("s.where"), t("s.where.help"), anchorOptions()),
        sliderRow("overlay", "Width", t("s.size"), null, 10, 150, cm, { factor: 100 }),
        row(t("app.colors"), null, themeChips("overlay", "Theme", PANEL_THEME_ORDER, v => t("theme." + v),
          v => v === 6 ? customPalette(o.CustomBackground || "#0e1116", o.CustomText || "#f0f3f8", o.CustomAccent || "#96bef0") : PANEL_THEMES[v]), { stack: true }),
      ]),
      group(t("nav.watch"), t("s.watch.help"), [
        switchCtl("overlay", "WatchEnabled", t("watch.show")),
        segRow("overlay", "WatchStyle", t("watch.style"), null, [0, 1, 2].map(v => ({ value: v, label: t("wstyle." + v) })), { enabled: () => !!get("overlay", "WatchEnabled") }),
        segRow("overlay", "WatchAnchor", t("watch.anchor"), null, anchorOptions(), { enabled: () => !!get("overlay", "WatchEnabled") }),
      ]),
      group(t("s.window"), t("s.window.help"), [
        switchCtl("overlay", "WindowEnabled", t("s.window.show"), t("home.window.help")),
        row(t("s.window.pick"), null, h("button", { class: "btn", type: "button", onclick: () => go("window") }, t("s.window.pick.button"))),
      ]),
      moreLink("s.more.headset", "panel"),
    ];
    settingsLayout(page, form, panelStage());
  }

  function renderSimpleVideo(page) {
    refreshers = [];
    const snapKey = get("desktop", "SnapHotkey");
    const form = [
      group(t("s.video"), t("s.video.help"), [
        switchCtl("overlay", "VideoCapture", t("vid.enable"), t("s.video.enable.help")),
        segRow("desktop", "VideoTarget", t("s.video.target"), null, [{ value: "file", label: t("home.share.file") }, { value: "stream", label: t("home.share.live") }],
          { enabled: videoEnabled }),
        h("div", { class: "video-card", id: "video-card", "aria-live": "polite" }),
        row(t("snap.title"), snapKey ? t("s.snap.help", { key: hotkeyText(snapKey) }) : null,
          h("button", { class: "btn", type: "button", disabled: !videoEnabled(), onclick: () => send("snapshot") }, icon("camera"), t("snap.take")), { enabled: videoEnabled }),
      ]),
      group(t("s.quality"), t("s.quality.help"), [
        row("", null, choiceCards("desktop", QUALITY_CHOICES, "quality.", { recommended: "high" }), { stack: true }),
        segRow("desktop", "VideoAspect", t("vid.aspect"), t("s.aspect.help"), ["16:9", "4:3", "1:1"].map(a => ({ value: a, label: t("s.aspect." + a.replace(":", "")) }))),
      ], videoEnabled),
      group(t("vid.sound"), null, [
        switchCtl("desktop", "VideoGameAudio", t("vid.game")),
        switchCtl("desktop", "VideoMic", t("vid.mic")),
      ], videoEnabled),
      group(t("s.live"), t("s.live.help"), [
        segRow("desktop", "StreamService", t("vid.stream.service"), null, ["twitch", "youtube", "both"].map(x => ({ value: x, label: t("vid.stream.service." + x) }))),
        streamKeyRow("twitch"),
        streamKeyRow("youtube"),
      ], () => videoEnabled() && streaming()),
      group(t("s.files"), null, [
        row(t("s.files.videos"), null, h("button", { class: "btn", type: "button", onclick: () => send("videoOpenFolder") }, icon("folder"), t("vid.folder.open"))),
        row(t("s.files.snaps"), null, h("button", { class: "btn", type: "button", onclick: () => send("snapOpenFolder") }, icon("folder"), t("vid.folder.open"))),
      ]),
      moreLink("s.more.video", "video"),
    ];
    settingsLayout(page, form, null);
    refreshers.push(updateVideo);
    updateVideo();
  }

  function renderSimpleImage(page) {
    refreshers = [];
    const mode = () => get("overlay", "ImageMode") || "off";
    const compare = get("overlay", "Hotkey.image");
    const form = [
      group(t("s.image"), t("s.image.help"), [row("", null, presetChips(0), { stack: true })]),
      group(t("s.image.where"), null, [
        liveHelp(segRow("overlay", "ImageMode", t("img.mode"), " ", ["off", "headset", "video"].map(m => ({ value: m, label: t("img.mode." + m) }))),
          () => t("img.mode.help." + mode())),
        compare ? row(t("img.compare"), t("img.compare.help"), h("span", { class: "combo" }, hotkeyParts(compare).map(x => h("kbd", { text: x })))) : null,
      ]),
      moreLink("s.more.image", "image"),
    ];
    settingsLayout(page, form, null);
  }

  // --- Bienvenue (premier lancement, mode simple) ------------------------------------------------------

  function openWelcome() {
    let step = 0;
    const finish = () => { closeModal(); setValue("desktop", "UiWelcome", true); go("home"); };
    const draw = () => {
      const saved = refreshers;
      refreshers = [];
      const steps = [
        h("div", { class: "welcome" }, h("p", { class: "lead", text: t("w.1.text") }),
          h("div", { class: "quick" }, layerSwitch(t("layer.label"), true), switchCtl("overlay", "OpenVR", t("panel.openvr.label"), t("panel.openvr.help")))),
        h("div", { class: "welcome" }, h("p", { class: "lead", text: t("w.2.text") }),
          choiceCards("overlay", CONTENT_CHOICES, "content.", { recommended: "essential" }),
          h("div", { class: "quick" }, switchCtl("overlay", "Visible", t("s.perf.show"), t("home.panel.help", { key: hotkeyText(get("overlay", "Hotkey.toggle") || 0) || "--" })),
            switchCtl("overlay", "WatchEnabled", t("home.watch")))),
        h("div", { class: "welcome" }, h("p", { class: "lead", text: t("w.3.text") }),
          h("div", { class: "quick" }, switchCtl("overlay", "VideoCapture", t("vid.enable"), t("w.3.keys", {
            video: hotkeyText(get("desktop", "VideoHotkey") || 0) || "--", snap: hotkeyText(get("desktop", "SnapHotkey") || 0) || "--" }))),
          choiceCards("desktop", QUALITY_CHOICES, "quality.", { recommended: "high" })),
      ];
      const ownRefreshers = refreshers;
      refreshers = saved;
      const dots = h("ol", { class: "steps", "aria-label": t("w.steps") }, [0, 1, 2].map(i => h("li", { "aria-current": i === step ? "step" : null, text: t("w.step." + (i + 1)) })));
      const foot = [
        h("button", { class: "btn ghost", type: "button", onclick: finish, text: t("w.skip") }),
        h("span", { style: { flex: "1" } }),
        step > 0 ? h("button", { class: "btn", type: "button", onclick: () => { step--; draw(); }, text: t("w.back") }) : null,
        h("button", { class: "btn primary", type: "button", onclick: () => { if (step < 2) { step++; draw(); } else finish(); }, text: t(step < 2 ? "w.next" : "w.done") }),
      ];
      const box = openModal(t("w.title." + (step + 1)), h("div", null, dots, steps[step]), { wide: true, foot, noClose: true });
      // Les contrôles de la fenêtre se mettent à jour entre eux (et ceux de la page, derrière).
      box.addEventListener("click", () => setTimeout(() => ownRefreshers.forEach(f => f()), 0));
    };
    draw();
  }

  const PAGE_BUILDERS = {
    dashboard: renderDashboard, home: renderHome, "simple-headset": renderSimpleHeadset, "simple-video": renderSimpleVideo, "simple-image": renderSimpleImage,
    panel: page => { page.appendChild(tabBar("panel")); (state.tab.panel === "appearance" ? renderAppearancePage : renderPanelPage)(page); },
    watch: renderWatchPage, window: renderWindowPage, image: renderImagePage, video: renderVideoPage, desktop: renderDesktopPage,
    reports: renderReportsPage,
    diagnostics: page => { page.appendChild(tabBar("diagnostics")); (state.tab.diagnostics === "measures" ? renderMeasuresPage : renderConfigPage)(page); },
    hotkeys: renderHotkeysPage, interface: renderInterfacePage,
  };

  function toast(text) {
    const el = document.getElementById("toast");
    el.textContent = text;
    el.hidden = false;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => (el.hidden = true), 2600);
  }

  document.addEventListener("keydown", e => {
    if (hotkeyCapture) return;
    const typing = /^(INPUT|TEXTAREA|SELECT)$/.test((e.target && e.target.tagName) || "");
    if (e.key === "F11") { e.preventDefault(); toggleFullscreen(); }
    else if ((e.ctrlKey || e.metaKey) && !e.altKey && (e.key === "k" || e.key === "K")) { e.preventDefault(); if (search) closeSearch(); else openSearch(); }
    else if (e.key === "Escape" && modal && !search) { if (modal.back.querySelector(".modal-head .btn")) closeModal(); }
    else if (e.key === "?" && !typing && !modal && !search && state.ready) { e.preventDefault(); openCheatSheet(); }
    else if (e.key === "Escape" && state.fullscreen && !modal && !search) { send("fullscreen", { on: false }); }
  });

  // Démarrage : le programme répond à "ready" par "init".
  function start() { send("ready"); }

  if (host) start();
  else {
    // Mise au point dans un navigateur : programme simulé.
    const s = document.createElement("script");
    s.src = "dev/mock.js";
    s.onload = start;
    document.body.appendChild(s);
  }

  return { receive, num, state, go };
})();
