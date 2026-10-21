// Lecture du relevé de configuration (envoyé par XRPerfSensors, ConfigProbe.cs) : reconnaissance des
// runtimes et des layers OpenXR, constats et texte à copier. Aucun accès à la page ni au programme :
// ce fichier est aussi chargé tel quel par les essais hors Windows (tests/config_check.js).
//
// Les constats restent prudents et ne modifient rien : ils disent ce qui est vu et, si besoin, quoi vérifier.
"use strict";

(function (root) {
  // Layers OpenXR connues, reconnues par un morceau de leur nom (api_layer.name) ou de leur fichier .json.
  // category : framegen (génération d'images), foveated (rendu fovéal), toolkit (outil à tout faire),
  // overlay (affichage dans le casque), tracking (suivi, compensation de mouvement), postfx (retouche d'image).
  const KNOWN_LAYERS = [
    { id: "xrperf", match: ["perf_overlay"], label: "XR Perf Overlay", category: "overlay" },
    { id: "toolkit", match: ["mbucchia_toolkit", "openxr-toolkit", "openxr_toolkit"], label: "OpenXR Toolkit", category: "toolkit" },
    { id: "quadviews", match: ["quad_views", "quadviews", "quad-views"], label: "Quad-Views-Foveated", category: "foveated" },
    { id: "ofxr", match: ["ofxr"], label: "OFXR Bridge", category: "framegen" },
    { id: "kneeboard", match: ["openkneeboard"], label: "OpenKneeboard", category: "overlay" },
    { id: "obsmirror", match: ["obsmirror", "obs_mirror"], label: "OBS Mirror", category: "overlay" },
    { id: "motioncomp", match: ["motion_compensation", "motioncompensation"], label: "OpenXR-MotionCompensation", category: "tracking" },
    { id: "necksafer", match: ["necksafer"], label: "XRNeckSafer", category: "tracking" },
    { id: "ultraleap", match: ["ultraleap"], label: "Ultraleap", category: "tracking" },
    { id: "eyetrackers", match: ["eye_trackers", "eye-trackers"], label: "OpenXR-Eye-Trackers", category: "tracking" },
    { id: "htcc", match: ["handtrackedcockpit"], label: "Hand-Tracked Cockpit Clicking", category: "tracking" },
    { id: "reshade", match: ["reshade"], label: "ReShade", category: "postfx" },
  ];

  const POWER_PLANS = {
    "a1841308-3541-4fab-bc81-f71556f20b4a": "saver",
    "381b4222-f694-41f0-9685-ff5bb260df2e": "balanced",
    "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c": "high",
    "e9a42b02-d5df-448d-aa00-03f14749eb61": "ultimate",
  };

  const lower = s => String(s || "").toLowerCase();

  // Famille d'un runtime OpenXR, d'après son nom et son chemin (ou la ligne « Runtime OpenXR » d'un rapport).
  function runtimeKind(text) {
    const s = lower(text);
    if (!s) return "";
    if (s.includes("pimaxxr") || s.includes("pimax-openxr")) return "pimaxxr";
    if (s.includes("pimax")) return "pimax";
    if (s.includes("steamxr") || s.includes("steamvr")) return "steamvr";
    if (s.includes("virtualdesktop") || s.includes("virtual desktop") || s.includes("vdxr")) return "vdxr";
    if (s.includes("oculus") || s.includes("meta ")) return "oculus";
    if (s.includes("mixedreality") || s.includes("mixed reality")) return "wmr";
    if (s.includes("varjo")) return "varjo";
    if (s.includes("monado")) return "monado";
    return "other";
  }

  function runtimeText(rt) {
    return rt ? (rt.name || "") + " " + (rt.path || "") + " " + (rt.library || "") : "";
  }

  function fileName(path) {
    const s = String(path || "");
    const i = Math.max(s.lastIndexOf("\\"), s.lastIndexOf("/"));
    return i >= 0 ? s.slice(i + 1) : s;
  }

  function identifyLayer(layer) {
    const s = lower((layer.name || "") + " " + fileName(layer.manifest));
    for (const k of KNOWN_LAYERS) if (k.match.some(m => s.includes(m))) return k;
    return null;
  }

  function layerLabel(layer) {
    const k = identifyLayer(layer);
    if (k) return k.label;
    return layer.name || fileName(layer.manifest).replace(/\.json$/i, "") || "?";
  }

  // État d'une inscription : missing (fichier .json absent), unreadable, nolib (DLL introuvable),
  // off (désactivée dans le registre ou par une variable d'environnement), on.
  function layerStatus(layer) {
    if (!layer.exists) return "missing";
    if (!layer.readable) return "unreadable";
    if (!layer.enabled) return "off";
    if (layer.disabledByEnvironment || layer.enabledByEnvironment === false) return "off";
    if (layer.libraryExists === false) return "nolib";
    return "on";
  }

  function gpuVendor(name) {
    const s = lower(name);
    if (s.includes("nvidia") || s.includes("geforce") || s.includes("quadro")) return "nvidia";
    if (s.includes("amd") || s.includes("radeon")) return "amd";
    if (s.includes("intel")) return "intel";
    return "other";
  }

  // Puce graphique intégrée au processeur (d'après son nom).
  function isIntegrated(name) {
    const s = lower(name);
    if (gpuVendor(name) === "intel") return !s.includes(" arc");
    if (gpuVendor(name) === "amd") return /radeon\(tm\) graphics|radeon graphics|radeon\(tm\) [0-9]+m|vega [0-9]+ graphics/.test(s) && !/rx ?[0-9]/.test(s);
    return false;
  }

  // Version de pilote NVIDIA telle qu'affichée par NVIDIA : 32.0.15.6614 -> 566.14 (cinq derniers chiffres).
  function driverLabel(gpu) {
    const v = String(gpu.driverVersion || "");
    if (!v) return "";
    if (gpuVendor(gpu.name) === "nvidia") {
      const parts = v.split(".");
      if (parts.length === 4 && parts.every(p => /^\d+$/.test(p))) {
        const digits = (parts[2] + parts[3].padStart(4, "0")).slice(-5);
        if (digits.length === 5) return digits.slice(0, 3) + "." + digits.slice(3);
      }
    }
    return v;
  }

  // Carte qui fait le rendu : celle suivie par les capteurs si on la retrouve, sinon celle qui a le plus de mémoire.
  function mainGpu(cfg) {
    const gpus = (cfg.gpus || []).filter(g => g && g.name);
    if (!gpus.length) return null;
    if (cfg.sensorGpu) {
      const match = gpus.find(g => lower(g.name) === lower(cfg.sensorGpu)) || gpus.find(g => lower(cfg.sensorGpu).includes(lower(g.name)) || lower(g.name).includes(lower(cfg.sensorGpu)));
      if (match) return match;
    }
    return gpus.slice().sort((a, b) => (b.vramGb || 0) - (a.vramGb || 0) || (isIntegrated(a.name) ? 1 : 0) - (isIntegrated(b.name) ? 1 : 0))[0];
  }

  function powerPlan(cfg) {
    return POWER_PLANS[lower(cfg.powerPlanId)] || (cfg.powerPlanId ? "custom" : "");
  }

  function headsetSoftware(cfg) {
    return (cfg.software || []).find(s => /pimax play|pimaxclient|pitool/i.test(s.name || "") && !/pimaxxr/i.test(s.name || "")) || null;
  }

  const ORDER = { Problem: 0, Warning: 1, Info: 2, Good: 3 };

  // Inscription que la page peut activer ou désactiver : fichier présent et lisible. Celle de cette
  // installation de XR Perf Overlay a son propre interrupteur (tableau de bord, page Panneau).
  function canToggle(layer) {
    return !!layer && layer.id != null && layer.exists && layer.readable && !(layer.ours && layer.current);
  }

  // Action proposée avec un constat : désactiver cette layer.
  function offAction(layer) {
    return canToggle(layer) && layer.enabled ? { id: layer.id, manifest: layer.manifest, on: false, label: layerLabel(layer) } : null;
  }

  // Constats, du plus important au moins important : [{ level, key, text, action }]. action : { id, on, label }
  // pour activer ou désactiver une layer (bouton à côté du constat), ou absent.
  function analyze(cfg, t) {
    const out = [];
    const add = (level, key, params, action) => out.push({ level, key: key.replace("cfg.f.", "").replace(/\.(one|other)$/, ""), text: t(key, params), action: action || null });
    if (!cfg) return [{ level: "Problem", key: "failed", text: t("cfg.f.failed") }];

    const layers = cfg.layers || [];
    const rt = cfg.activeRuntime;
    const kind = runtimeKind(runtimeText(rt));
    const rtName = rt ? rt.name || fileName(rt.path) : "";

    // 1. Runtime OpenXR.
    if (cfg.envRuntime) add("Warning", "cfg.f.envRuntime", { path: cfg.envRuntime });
    if (!rt) add("Problem", "cfg.f.noRuntime");
    else if (!rt.exists) add("Problem", "cfg.f.runtimeMissing", { path: rt.path });
    else if (rt.libraryExists === false) add("Problem", "cfg.f.runtimeLib", { name: rtName, path: rt.library || "" });
    else add("Good", "cfg.f.runtime", { name: rtName || t("cfg.rt." + kind) });
    if (rt && rt.exists && kind === "steamvr" && headsetSoftware(cfg)) add("Info", "cfg.f.steamvrActive", { soft: headsetSoftware(cfg).name });

    const last = cfg.lastSession;
    if (last && !last.openVR && rt && rt.exists) {
      const lastKind = runtimeKind(last.runtime);
      if (lastKind && lastKind !== "other" && kind && kind !== "other" && lastKind !== kind)
        add("Info", "cfg.f.runtimeChanged", { game: last.game || "?", last: t("cfg.rt." + lastKind), now: t("cfg.rt." + kind) });
    }

    // 2. Layers OpenXR.
    const ours = layers.filter(l => l.ours);
    const current = ours.find(l => l.current) || null;
    if (!ours.length) add("Warning", "cfg.f.oursMissing");
    else if (current && !current.enabled) add("Info", "cfg.f.oursOff");
    const staleOurs = ours.filter(l => !l.current && l.exists && current);
    if (staleOurs.length) add("Warning", "cfg.f.oursDuplicate", { paths: staleOurs.map(l => l.manifest).join(" ; ") });

    for (const l of layers) {
      const label = layerLabel(l);
      const status = layerStatus(l);
      if (status === "missing") add("Warning", "cfg.f.layerOrphan", { name: label, path: l.manifest });
      else if (status === "unreadable") add("Warning", "cfg.f.layerUnreadable", { name: label, path: l.manifest });
      else if (status === "nolib") add("Warning", "cfg.f.layerLib", { name: label, path: l.library || "" }, offAction(l));
      else if (l.enabled && l.disabledByEnvironment) add("Info", "cfg.f.layerEnvOff", { name: label, env: l.disableEnvironment });
      else if (l.enabled && l.enabledByEnvironment === false) add("Info", "cfg.f.layerEnvNeeded", { name: label, env: l.enableEnvironment });
    }

    const active = layers.filter(l => layerStatus(l) === "on");
    const seen = {};
    for (const l of active) {
      if (l.ours || !l.name) continue;
      const key = lower(l.name);
      if (seen[key]) add("Warning", "cfg.f.layerTwice", { name: layerLabel(l) });
      seen[key] = true;
    }
    const byId = id => active.filter(l => (identifyLayer(l) || {}).id === id);
    if (byId("toolkit").length) add("Warning", "cfg.f.toolkit", null, offAction(byId("toolkit")[0]));
    if (byId("quadviews").length && kind === "pimax") add("Info", "cfg.f.quadviewsPimax", null, offAction(byId("quadviews")[0]));
    const framegen = active.filter(l => (identifyLayer(l) || {}).category === "framegen");
    if (framegen.length >= 2) add("Warning", "cfg.f.framegenTwice", { names: framegen.map(layerLabel).join(", ") });
    const userLayers = layers.filter(l => l.scope === "user");
    if (userLayers.length) add("Info", "cfg.f.userLayers." + (userLayers.length === 1 ? "one" : "other"), { n: userLayers.length });
    if (cfg.envLayers) add("Info", "cfg.f.envLayers", { value: cfg.envLayers });

    // 3. Jeux SteamVR (OpenVR).
    if (cfg.openVrRuntime) {
      if (lower(cfg.openVrRuntime).includes("opencomposite")) add("Info", "cfg.f.opencomposite");
      else if (cfg.openVrRuntimeExists === false) add("Warning", "cfg.f.openvrMissing", { path: cfg.openVrRuntime });
    }

    // 4. Matériel.
    if (cfg.ramGb != null && cfg.ramGb < 15.5) add("Warning", "cfg.f.ramLow", { gb: fmt(cfg.ramGb, 0) });
    const gpu = mainGpu(cfg);
    if (gpu && gpu.vramGb != null && gpu.vramGb < 7.5 && !isIntegrated(gpu.name)) add("Warning", "cfg.f.vramLow", { name: gpu.name, gb: fmt(gpu.vramGb, 0) });
    const gpus = (cfg.gpus || []).filter(g => g && g.name);
    const igpu = gpus.find(g => isIntegrated(g.name));
    const dgpu = gpus.find(g => !isIntegrated(g.name) && gpuVendor(g.name) !== "other");
    if (igpu && dgpu) add("Info", "cfg.f.hybrid", { igpu: igpu.name, dgpu: dgpu.name });

    // 5. Windows.
    if (powerPlan(cfg) === "saver") add("Warning", "cfg.f.powerSaver");
    if (cfg.hags === "off" && gpu && gpuVendor(gpu.name) === "nvidia") add("Info", "cfg.f.hagsOff");
    if (cfg.gameMode === "off") add("Info", "cfg.f.gameModeOff");

    if ((cfg.errors || []).length) add("Info", "cfg.f.partial", { parts: cfg.errors.join(", ") });
    if (!out.some(f => f.level === "Problem" || f.level === "Warning")) add("Good", "cfg.f.allGood");

    return out.map((f, i) => ({ f, i })).sort((a, b) => ORDER[a.f.level] - ORDER[b.f.level] || a.i - b.i).map(x => x.f);
  }

  function fmt(v, digits) {
    if (v == null || isNaN(v)) return "--";
    return Number(v).toFixed(digits || 0);
  }

  // Valeurs à afficher, communes à la page et au texte copié : [{ title, rows: [{ label, value, sub }] }].
  function sections(cfg, t, num) {
    num = num || fmt;
    const rt = cfg.activeRuntime;
    const out = [];
    const chain = [];
    chain.push({ label: t("cfg.openxr"), value: rt ? rt.name || t("cfg.rt." + runtimeKind(runtimeText(rt))) : t("cfg.none"), sub: rt ? rt.path : "" });
    const others = (cfg.availableRuntimes || []).filter(r => !rt || lower(r.path) !== lower(rt.path));
    if (others.length) chain.push({ label: t("cfg.otherRuntimes"), value: others.map(r => r.name || t("cfg.rt." + runtimeKind(runtimeText(r)))).join(", "), sub: "" });
    if (cfg.openVrRuntime) {
      const oc = lower(cfg.openVrRuntime).includes("opencomposite");
      chain.push({ label: t("cfg.openvr"), value: oc ? "OpenComposite" : lower(cfg.openVrRuntime).includes("steamvr") ? "SteamVR" : fileName(cfg.openVrRuntime), sub: cfg.openVrRuntime });
    }
    const soft = headsetSoftware(cfg);
    if (soft) chain.push({ label: t("cfg.headsetSoftware"), value: soft.name + (soft.version ? " " + soft.version : ""), sub: "" });
    if (cfg.lastSession) {
      const l = cfg.lastSession;
      chain.push({ label: t("cfg.lastSession"), value: l.game || "?", sub: [l.headset, l.runtime, (l.start || "").replace("T", " ").slice(0, 16)].filter(Boolean).join(" · ") });
    }
    if (cfg.envRuntime) chain.push({ label: "XR_RUNTIME_JSON", value: fileName(cfg.envRuntime), sub: cfg.envRuntime });
    if (cfg.envLayers) chain.push({ label: "XR_ENABLE_API_LAYERS", value: cfg.envLayers, sub: "" });
    out.push({ id: "chain", title: t("cfg.chain"), help: "", rows: chain });

    out.push({
      id: "layers", title: t("cfg.layers"), help: t("cfg.layers.help"),
      rows: (cfg.layers || []).map(l => ({
        label: layerLabel(l), value: t("cfg.layer." + layerStatus(l)), status: layerStatus(l),
        toggle: canToggle(l) ? { id: l.id, manifest: l.manifest, on: !l.enabled, label: layerLabel(l) } : null,
        tags: [l.ours && l.current ? t("cfg.tag.thisInstall") : null, l.scope === "user" ? t("cfg.tag.user") : null].filter(Boolean),
        sub: (l.description ? l.description + " · " : "") + (l.manifest || ""),
      })),
    });

    const sw = (cfg.software || []).map(s => ({ label: s.name, value: s.version || "", sub: "" }));
    if (sw.length) out.push({ id: "software", title: t("cfg.software"), help: "", rows: sw });

    const hw = [];
    if (cfg.cpu) hw.push({ label: t("cfg.cpu"), value: cfg.cpu, sub: cfg.threads ? t("cfg.threads", { n: cfg.threads }) : "" });
    if (cfg.ramGb != null) hw.push({ label: t("cfg.ram"), value: num(cfg.ramGb, 0) + " " + t("unit.gb"), sub: "" });
    for (const g of (cfg.gpus || [])) {
      const parts = [];
      if (g.vramGb != null) parts.push(num(g.vramGb, 0) + " " + t("unit.gb") + " " + t("cfg.vram"));
      if (driverLabel(g)) parts.push(t("cfg.driver") + " " + driverLabel(g));
      hw.push({ label: t("cfg.gpu"), value: g.name, sub: parts.join(" · ") });
    }
    out.push({ id: "hardware", title: t("cfg.hardware"), help: "", rows: hw });

    const win = [];
    if (cfg.windows) win.push({ label: t("cfg.windows"), value: cfg.windows + (cfg.windowsVersion ? " " + cfg.windowsVersion : ""), sub: cfg.windowsBuild ? t("cfg.build", { n: cfg.windowsBuild }) : "" });
    win.push({ label: t("cfg.hags"), value: t("cfg.hags." + (cfg.hags || "default")), sub: t("cfg.hags.help") });
    if (cfg.gameMode) win.push({ label: t("cfg.gameMode"), value: t("cfg.onoff." + (cfg.gameMode === "off" ? "off" : "on")), sub: "" });
    const plan = powerPlan(cfg);
    if (plan) win.push({ label: t("cfg.power"), value: plan === "custom" ? cfg.powerPlanName || t("cfg.power.custom") : t("cfg.power." + plan), sub: "" });
    out.push({ id: "windows", title: t("cfg.win"), help: "", rows: win });
    return out;
  }

  // Diagnostic en texte simple, pour le copier (forum, Discord). Aucun nom de compte : les chemins du
  // dossier de l'utilisateur arrivent déjà sous la forme %USERPROFILE%.
  function text(cfg, t, version) {
    const lines = ["XR Perf Overlay " + (version || "") + " - " + t("cfg.textTitle"), ""];
    const mark = { Problem: "[!!]", Warning: "[!] ", Info: "[i] ", Good: "[ok]" };
    lines.push(t("cfg.findings").toUpperCase());
    for (const f of analyze(cfg, t)) lines.push("  " + mark[f.level] + " " + f.text);
    if (!cfg) return lines.join("\r\n") + "\r\n";
    for (const s of sections(cfg, t)) {
      lines.push("", s.title.toUpperCase());
      for (const r of s.rows) {
        lines.push("  " + r.label + " : " + (r.value || "--") + (r.tags && r.tags.length ? " (" + r.tags.join(", ") + ")" : ""));
        if (r.sub) lines.push("      " + r.sub);
      }
    }
    return lines.join("\r\n") + "\r\n";
  }

  const api = { analyze, sections, text, canToggle, runtimeKind, identifyLayer, layerLabel, layerStatus, driverLabel, gpuVendor, isIntegrated, mainGpu, powerPlan, KNOWN_LAYERS };
  root.ConfigCheck = api;
  if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof window !== "undefined" ? window : globalThis);
