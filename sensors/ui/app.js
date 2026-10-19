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
    panel: '<rect x="3" y="5" width="18" height="12" rx="2.5"/><path d="M7 9.5h4M7 13h7M15 9.5h2"/>',
    appearance: '<path d="M12 3a9 9 0 1 0 0 18c1.2 0 1.6-.8 1.6-1.6 0-1.3-1-1.5-1-2.6 0-1 .8-1.8 1.8-1.8H17a4 4 0 0 0 4-4C21 6.6 17 3 12 3z"/><circle cx="7.5" cy="11" r="1.2"/><circle cx="10.5" cy="7.2" r="1.2"/><circle cx="15" cy="7.6" r="1.2"/>',
    watch: '<circle cx="12" cy="12" r="6"/><path d="M12 9.5V12l1.6 1.2M9 6.5 9.6 3h4.8l.6 3.5M9 17.5l.6 3.5h4.8l.6-3.5"/>',
    window: '<rect x="3" y="4" width="18" height="16" rx="2.5"/><path d="M3 8.5h18M6.5 6.3h.01M9 6.3h.01"/><path d="M8 13h8M8 16h5"/>',
    mirror: '<rect x="2.5" y="4" width="19" height="13" rx="2"/><path d="M9 21h6M12 17v4"/><rect x="6" y="7.5" width="7" height="4.5" rx="1"/>',
    measures: '<path d="M4 18a8 8 0 1 1 16 0"/><path d="M12 18l4-6"/><path d="M4 18h2M18 18h2M12 10V8"/>',
    reports: '<path d="M7 3h7l5 5v13H7z"/><path d="M14 3v5h5"/><path d="M10 17v-3M13 17v-5M16 17v-2"/>',
    interface: '<path d="M4 7h10M18 7h2M4 17h4M12 17h8"/><circle cx="16" cy="7" r="2"/><circle cx="10" cy="17" r="2"/>',
    expand: '<path d="M4 9V4h5M20 9V4h-5M4 15v5h5M20 15v5h-5"/>',
    compress: '<path d="M9 4v5H4M15 4v5h5M9 20v-5H4M15 20v-5h5"/>',
    refresh: '<path d="M20 11a8 8 0 1 0-2.3 5.7"/><path d="M20 4v7h-7"/>',
    trash: '<path d="M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3"/>',
    folder: '<path d="M3 6.5A1.5 1.5 0 0 1 4.5 5H9l2 2.5h8.5A1.5 1.5 0 0 1 21 9v9.5a1.5 1.5 0 0 1-1.5 1.5h-15A1.5 1.5 0 0 1 3 18.5z"/>',
    text: '<path d="M7 3h7l5 5v13H7z"/><path d="M14 3v5h5M10 12h6M10 15h6M10 18h4"/>',
    alert: '<path d="M12 4 2.5 20h19z"/><path d="M12 10v4.5M12 17.5h.01"/>',
    good: '<circle cx="12" cy="12" r="9"/><path d="m8 12.5 2.7 2.7L16.5 9"/>',
    info: '<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7.5h.01"/>',
    problem: '<circle cx="12" cy="12" r="9"/><path d="m9 9 6 6M15 9l-6 6"/>',
    tip: '<path d="M9 18h6M10 21h4M12 3a6 6 0 0 0-3.6 10.8c.6.5 1.1 1.3 1.1 2.2h5c0-.9.5-1.7 1.1-2.2A6 6 0 0 0 12 3z"/>',
    compare: '<path d="M8 4v16M16 4v16M4 8h4M16 16h4M4 14h4M16 10h4"/>',
  };

  function icon(name) {
    const span = document.createElement("span");
    span.innerHTML = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">' + (ICONS[name] || "") + "</svg>";
    return span.firstChild;
  }

  // --- Échanges avec le programme -----------------------------------------------------------------

  function send(type, payload) {
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
        state.lang = msg.lang === "en" ? "en" : "fr";
        state.readMode = msg.readMode === "detailed" ? "detailed" : "simple";
        state.ready = true;
        document.getElementById("app").hidden = false;
        applyTheme();
        renderAll();
        if (msg.page) go(msg.page);
        break;
      case "overlay":
      case "desktop": {
        // Réglages changés ailleurs (raccourcis en jeu, menu de l'icône) : seules les pages qui les montrent sont redessinées.
        const key = msg.type;
        const fresh = msg[key] || state[key];
        const changed = JSON.stringify(fresh) !== JSON.stringify(state[key]);
        state[key] = fresh;
        if (!changed) break;
        applyTheme();
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
        if (state.page === "dashboard") renderPage();
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
      case "layer":
        // Layer activée ou désactivée (ici, depuis le menu de l'icône ou par l'installateur).
        if (!msg.layer || JSON.stringify(msg.layer) === JSON.stringify(state.layer)) break;
        state.layer = msg.layer;
        if ((state.page === "dashboard" || state.page === "panel") && !isDragging()) renderPage();
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
  ];
  const UI_THEMES = ["panel", "dark", "light", "contrast", "green", "amber", "violet", "custom"];

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
      return PANEL_THEMES[id] || PANEL_THEMES[0];
    }
    if (theme === "custom") return customPalette(d.UiBackground || "#0e1116", d.UiText || "#f0f3f8", d.UiAccent || "#96bef0");
    const index = UI_THEMES.indexOf(theme) - 1;
    return PANEL_THEMES[index] || PANEL_THEMES[0];
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
    document.documentElement.style.colorScheme = light ? "light" : "dark";
    const scale = Math.max(80, Math.min(140, state.desktop.UiScale || 100)) / 100;
    set("--ui-scale", String(scale));
    document.documentElement.classList.toggle("no-anim", state.desktop.UiAnimations === false);
    document.documentElement.lang = state.lang;
    send("chrome", { background: bg, light });
  }

  const cssVar = name => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

  // --- Navigation -----------------------------------------------------------------------------------

  const PAGES = [
    { id: "dashboard", icon: "dashboard" },
    { group: "nav.group.headset" },
    { id: "panel", icon: "panel" },
    { id: "appearance", icon: "appearance" },
    { id: "watch", icon: "watch" },
    { id: "window", icon: "window" },
    { group: "nav.group.pc" },
    { id: "desktop", icon: "mirror" },
    { id: "measures", icon: "measures" },
    { id: "reports", icon: "reports" },
    { id: "interface", icon: "interface" },
  ];

  function go(page) {
    if (!PAGES.some(p => p.id === page)) page = "dashboard";
    state.page = page;
    send("page", { name: page });
    renderRail();
    renderTopbar();
    renderPage();
    const main = document.getElementById("page");
    main.scrollTop = 0;
    if (page === "reports") send("listReports");
    if (page === "window") send("listWindows");
  }

  function renderAll() {
    document.title = "XR Perf Overlay";
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

  function renderRail() {
    const rail = document.getElementById("rail");
    rail.replaceChildren();
    rail.appendChild(h("div", { class: "brand" }, brandMark(), h("div", { class: "brand-name" }, "XR Perf Overlay", h("span", { text: t("app.sub") }))));
    for (const p of PAGES) {
      if (p.group) { rail.appendChild(h("div", { class: "nav-group", text: t(p.group) })); continue; }
      const item = h("button", { class: "nav-item", type: "button", title: t("nav." + p.id), "aria-current": state.page === p.id ? "page" : null, onclick: () => go(p.id) },
        icon(p.icon), h("span", { text: t("nav." + p.id) }));
      if (p.id === "dashboard" && (!state.pawnio || versionMismatch())) item.appendChild(h("i", { class: "nav-badge", "aria-hidden": "true" }));
      rail.appendChild(item);
    }
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
    bar.replaceChildren(h("h1", { text: t("nav." + state.page) }), h("div", { class: "actions" }, pill, fs));
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
    const builder = {
      dashboard: renderDashboard, panel: renderPanelPage, appearance: renderAppearancePage, watch: renderWatchPage,
      window: renderWindowPage, desktop: renderDesktopPage, measures: renderMeasuresPage, reports: renderReportsPage, interface: renderInterfacePage,
    }[state.page];
    page.replaceChildren();
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
    return row(label, help, h("label", { class: "switch" }, input, h("span")), opts);
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
    return row(label, help, seg, opts);
  }

  function selectRow(scope, key, label, help, options, opts) {
    const sel = h("select", { class: "select", "aria-label": label });
    options.forEach(o => sel.appendChild(h("option", { value: String(o.value), text: o.label })));
    sel.value = String(get(scope, key));
    sel.disabled = locked(scope);
    sel.addEventListener("change", () => setValue(scope, key, typeof options[0].value === "number" ? Number(sel.value) : sel.value));
    return row(label, help, sel, opts);
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
    return row(label, help, h("div", { class: "slider" }, input, out), opts);
  }

  const cm = v => num(v) + " " + t("unit.cm");
  const signedCm = v => (v > 0 ? "+" : "") + num(v) + " " + t("unit.cm");
  const pct = v => num(v) + " " + t("unit.pct");
  const meters = v => num(v / 100, 2) + " " + t("unit.m");

  function group(title, help, children, enabled) {
    const g = h("section", { class: "group", "data-off": t("set.offWhen") },
      h("div", { class: "group-head" }, h("h2", { text: title }), help ? h("p", { text: help }) : null), children);
    if (enabled) {
      const f = () => g.setAttribute("aria-disabled", String(!enabled()));
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

  function renderDashboard(page) {
    refreshers = [];
    const dash = h("div", { class: "dash" });
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

    // Le panneau tel qu'il est dans le casque.
    const stageCard = h("section", { class: "card dash-stage" },
      h("div", { class: "card-head" }, h("h2", { text: t("dash.preview") }), h("span", { class: "muted small", id: "panel-caption" })),
      h("div", { class: "stage", id: "panel-stage" }, h("img", { id: "panel-img", alt: t("dash.preview") }), h("div", { class: "stage-empty", id: "panel-empty", hidden: true, text: t("dash.preview.none") })));
    dash.appendChild(stageCard);

    const quick = h("section", { class: "card" }, h("div", { class: "card-head" }, h("h2", { text: t("dash.quick") })),
      h("div", { class: "quick" },
        layerSwitch(t("dash.quick.layer"), false),
        switchCtl("overlay", "Visible", t("dash.quick.panel"), "Ctrl+Alt+O"),
        switchCtl("overlay", "WatchEnabled", t("dash.quick.watch")),
        switchCtl("overlay", "WindowEnabled", t("dash.quick.window"), "Ctrl+Alt+F"),
        switchCtl("overlay", "OpenVR", t("dash.quick.openvr")),
        switchCtl("desktop", "DesktopPanel", t("dash.quick.mirror"))));
    dash.appendChild(quick);

    // Capteurs en direct.
    const tiles = h("div", { class: "tiles" },
      h("div", { class: "tile", id: "tile-cpu" }),
      h("div", { class: "tile", id: "tile-gpu" }),
      h("div", { class: "tile", id: "tile-vram" }));
    dash.appendChild(h("section", { class: "card span" }, h("div", { class: "card-head" }, h("h2", { text: t("dash.sensors") })), tiles));

    // Dernière session.
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
    dash.appendChild(lastCard);

    // Raccourcis.
    const k = (...keys) => h("span", { class: "combo" }, ["Ctrl", "Alt"].concat(keys).map(x => h("kbd", { text: x })));
    dash.appendChild(h("section", { class: "card" }, h("div", { class: "card-head" }, h("h2", { text: t("dash.keys") })),
      h("div", { class: "keys" },
        k("O"), h("span", { text: t("key.toggle") }),
        k("P"), h("span", { text: t("key.position") }),
        k("R"), h("span", { text: t("key.reset") }),
        k(t("key.pgup") + " / " + t("key.pgdn")), h("span", { text: t("key.size") }),
        k(t("key.home") + " / " + t("key.end")), h("span", { text: t("key.opacity") }),
        k("F"), h("span", { text: t("key.window") }),
        k("G"), h("span", { text: t("key.place") }),
        k("S"), h("span", { text: t("key.record") }))));
    page.appendChild(dash);
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
      if (cap && state.page === "dashboard") cap.textContent = state.panelLive ? t("dash.preview.live") : t("dash.preview.sample");
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
    if (state.page === "dashboard") {
      const s = state.sensors || {};
      const hist = state.history;
      tile("tile-cpu", t("dash.cpu"), s.cpuName, num(s.cpuLoad), " %", [[num(s.cpuTemp) + " °C", t("dash.temp")], [num(s.cpuMHz), "MHz"]], hist.cpuLoad, "var(--s-cpu)", 0, 100);
      tile("tile-gpu", t("dash.gpu"), s.gpuName, num(s.gpuLoad), " %", [[num(s.gpuTemp) + " °C", t("dash.temp")], [num(s.gpuMHz), "MHz"]], hist.gpuLoad, "var(--s-gpu)", 0, 100);
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
        switchCtl("overlay", "Visible", t("panel.visible"), t("panel.visible.help")),
        segRow("overlay", "Anchor", t("panel.anchor"), null, anchorOptions()),
      ]),
      group(t("panel.view"), t("panel.view.help"), [
        row(t("panel.position"), t("panel.position.help"), spotPicker()),
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
        sliderRow("overlay", "Width", t("panel.width"), t("panel.width.help"), 10, 150, cm, { factor: 100 }),
        sliderRow("overlay", "GlobalOpacity", t("panel.globalOpacity"), t("panel.globalOpacity.help"), 20, 100, pct, { factor: 100 }),
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
        row("", null, themeChips("overlay", "Theme", [0, 1, 2, 3, 4, 5, 6], v => t("theme." + v),
          v => v === 6 ? customPalette(o.CustomBackground || "#0e1116", o.CustomText || "#f0f3f8", o.CustomAccent || "#96bef0") : PANEL_THEMES[v]), { stack: true }),
        customRow,
      ]),
      group(t("app.elements"), null, [
        switchCtl("overlay", "Clock", t("app.clock")),
        switchCtl("overlay", "HeadsetInfo", t("app.headset")),
        switchCtl("overlay", "PanelSession", t("app.session")),
        switchCtl("overlay", "PanelBattery", t("app.battery"), t("app.battery.help")),
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
      group(t("nav.window"), null, [switchCtl("overlay", "WindowEnabled", t("win.show"), t("win.show.help"))]),
      group(t("win.which"), t("win.which.help"), [row("", null, pick, { stack: true })]),
      group(t("win.where"), t("win.where.help"), [
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

  // --- Mesures --------------------------------------------------------------------------------

  // Mode des rapports : l'explication suit le mode choisi.
  function reportModeRow() {
    const modes = ["auto", "manual", "off"];
    const r = segRow("overlay", "ReportMode", t("meas.mode"), " ", modes.map(m => ({ value: m, label: t("meas.mode." + m) })), { stack: true });
    const help = r.querySelector(".row-help");
    const f = () => { help.textContent = t("meas.mode.help." + (modes.includes(get("overlay", "ReportMode")) ? get("overlay", "ReportMode") : "auto")); };
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
    const chips = themeChips("desktop", "UiTheme", UI_THEMES, v => (v === "panel" ? t("theme.panel") : v === "custom" ? t("theme.6") : t("theme." + (UI_THEMES.indexOf(v) - 1))),
      v => (v === "panel" ? panelPal() : v === "custom" ? customPalette(d.UiBackground || "#0e1116", d.UiText || "#f0f3f8", d.UiAccent || "#96bef0") : PANEL_THEMES[UI_THEMES.indexOf(v) - 1]));
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
    ];
    settingsLayout(page, form, null);
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
      const rows = [];
      if (n.cpuMsAvg != null && budget) {
        const share = Math.min(1.25, n.cpuMsAvg / budget);
        rows.push(barRow(t("rep.limit.cpu"), t("rep.limit.cpuSub"), share / 1.25 * 100, (1 / 1.25) * 100, share >= 0.85 ? "warn" : "", lim === "cpu" || lim === "both",
          num(n.cpuMsAvg, 1) + " " + t("unit.ms"), t("rep.limit.available", { ms: num(budget, 1) })));
      }
      if (n.gpuLoadAvg != null) {
        rows.push(barRow(t("rep.limit.gpu"), t("rep.limit.gpuSub"), n.gpuLoadAvg, null, n.gpuLoadAvg >= 90 ? "warn" : "", lim === "gpu" || lim === "both",
          num(n.gpuLoadAvg) + " %", n.gpuLoadMax != null ? t("rep.health.max") + " " + num(n.gpuLoadMax) + " %" : ""));
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
  function toast(text) {
    const el = document.getElementById("toast");
    el.textContent = text;
    el.hidden = false;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => (el.hidden = true), 2600);
  }

  document.addEventListener("keydown", e => {
    if (e.key === "F11") { e.preventDefault(); toggleFullscreen(); }
    else if (e.key === "Escape" && state.fullscreen) { send("fullscreen", { on: false }); }
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
