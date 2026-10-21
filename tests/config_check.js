// Essais de la lecture du relevé de configuration (sensors/ui/config.js), hors Windows :
//   node tests/config_check.js
// Chaque cas construit un relevé tel que l'envoie ConfigProbe.cs et vérifie les constats obtenus.
"use strict";

const assert = require("assert");
const path = require("path");

global.window = {};
require(path.join(__dirname, "..", "sensors", "ui", "i18n.js"));
const C = require(path.join(__dirname, "..", "sensors", "ui", "config.js"));

function translator(lang) {
  return (key, params) => {
    let s = window.I18N[lang][key];
    assert.ok(s != null, "texte manquant : " + key + " (" + lang + ")");
    if (params) for (const k in params) s = s.split("{" + k + "}").join(params[k]);
    assert.ok(!/\{[a-z]+\}/.test(s), "paramètre non remplacé dans " + key + " : " + s);
    return s;
  };
}
const fr = translator("fr"), en = translator("en");

const PIMAX = { path: "C:\\Program Files\\Pimax\\Runtime\\pimax_openxr.json", exists: true, readable: true, name: "Pimax OpenXR", library: "x.dll", libraryExists: true };
const STEAM = { path: "C:\\Steam\\steamapps\\common\\SteamVR\\steamxr_win64.json", exists: true, readable: true, name: "SteamVR", library: null, libraryExists: null };

function layer(name, manifest, extra) {
  return Object.assign({ scope: "machine", manifest, enabled: true, exists: true, readable: true, name, description: null, library: "a.dll", libraryExists: true,
    disableEnvironment: null, disabledByEnvironment: false, enableEnvironment: null, enabledByEnvironment: null, ours: false, current: false }, extra || {});
}
const OURS = layer("XR_APILAYER_NOVENDOR_perf_overlay", "C:\\XRPO\\XR_APILAYER_NOVENDOR_perf_overlay.json", { ours: true, current: true });

function base(extra) {
  return Object.assign({
    collectedAt: "2026-10-20T19:40:00", activeRuntime: PIMAX, envRuntime: null, envLayers: null, availableRuntimes: [PIMAX, STEAM],
    layers: [OURS], openVrRuntime: "C:\\Steam\\steamapps\\common\\SteamVR", openVrRuntimeExists: true,
    software: [{ name: "Pimax Play", version: "2.0.3" }], cpu: "AMD Ryzen 7 9800X3D 8-Core Processor", threads: 16, ramGb: 32,
    gpus: [{ name: "NVIDIA GeForce RTX 4090", driverVersion: "32.0.15.6614", vramGb: 24 }], sensorGpu: "NVIDIA GeForce RTX 4090",
    windows: "Windows 11 Pro", windowsVersion: "24H2", windowsBuild: "26100.1", hags: "on", gameMode: "default",
    powerPlanId: "381b4222-f694-41f0-9685-ff5bb260df2e", powerPlanName: null, lastSession: null, errors: [],
  }, extra || {});
}

function keysOf(cfg) {
  const findings = C.analyze(cfg, fr);
  return { keys: findings.map(f => f.key), texts: findings.map(f => f.level + ":" + f.text) };
}

let count = 0;
function check(title, cfg, expected, absent) {
  const { keys, texts } = keysOf(cfg);
  for (const k of expected) assert.ok(keys.includes(k), title + " : constat attendu « " + k + " » absent.\n  " + texts.join("\n  "));
  for (const k of absent || []) assert.ok(!keys.includes(k), title + " : constat « " + k + " » inattendu.\n  " + texts.join("\n  "));
  // Même chose en anglais, sans texte manquant ni paramètre oublié.
  assert.strictEqual(C.analyze(cfg, en).length, texts.length, title + " : nombre de constats différent en anglais");
  C.text(cfg, fr, "1.7.0");
  C.text(cfg, en, "1.7.0");
  count++;
}

// --- Reconnaissance --------------------------------------------------------------------------------

assert.strictEqual(C.runtimeKind("Pimax OpenXR C:\\Program Files\\Pimax\\Runtime\\pimax_openxr.json"), "pimax");
assert.strictEqual(C.runtimeKind("PimaxXR C:\\Program Files\\PimaxXR\\pimax-openxr.json"), "pimaxxr");
assert.strictEqual(C.runtimeKind("SteamVR/OpenXR 2.11.2"), "steamvr");
assert.strictEqual(C.runtimeKind("C:\\Program Files\\Virtual Desktop Streamer\\OpenXR\\virtualdesktop-openxr.json"), "vdxr");
assert.strictEqual(C.runtimeKind("Something"), "other");
assert.strictEqual(C.runtimeKind(""), "");
assert.strictEqual(C.driverLabel({ name: "NVIDIA GeForce RTX 4090", driverVersion: "32.0.15.6614" }), "566.14");
assert.strictEqual(C.driverLabel({ name: "NVIDIA GeForce RTX 3080", driverVersion: "31.0.15.3623" }), "536.23");
assert.strictEqual(C.driverLabel({ name: "NVIDIA GeForce RTX 5090", driverVersion: "32.0.15.7680" }), "576.80");
assert.strictEqual(C.driverLabel({ name: "AMD Radeon RX 7900 XTX", driverVersion: "32.0.12011.1036" }), "32.0.12011.1036");
assert.ok(C.isIntegrated("Intel(R) UHD Graphics 770"));
assert.ok(!C.isIntegrated("Intel(R) Arc(TM) A770 Graphics"));
assert.ok(C.isIntegrated("AMD Radeon(TM) Graphics"));
assert.ok(!C.isIntegrated("AMD Radeon RX 7900 XTX"));
assert.ok(!C.isIntegrated("NVIDIA GeForce RTX 4090"));
assert.strictEqual(C.layerLabel(layer("XR_APILAYER_MBUCCHIA_toolkit", "x.json")), "OpenXR Toolkit");
assert.strictEqual(C.layerLabel(layer(null, "C:\\a\\Some-Layer.json")), "Some-Layer");
assert.strictEqual(C.layerStatus(layer("a", "a.json", { enabled: false })), "off");
assert.strictEqual(C.layerStatus(layer("a", "a.json", { exists: false, readable: false })), "missing");
assert.strictEqual(C.layerStatus(layer("a", "a.json", { libraryExists: false })), "nolib");
assert.strictEqual(C.layerStatus(layer("a", "a.json", { libraryExists: false, enabled: false })), "off");
assert.strictEqual(C.mainGpu(base({ gpus: [{ name: "Intel(R) UHD Graphics 770", vramGb: 0.1 }, { name: "NVIDIA GeForce RTX 4070", vramGb: 12 }], sensorGpu: null })).name, "NVIDIA GeForce RTX 4070");

// --- Constats --------------------------------------------------------------------------------------

check("configuration saine", base(), ["runtime", "allGood"], ["steamvrActive", "toolkit", "ramLow", "vramLow", "hybrid", "powerSaver", "hagsOff", "oursMissing"]);
check("aucun runtime", base({ activeRuntime: null }), ["noRuntime"], ["allGood", "runtime"]);
check("runtime disparu", base({ activeRuntime: Object.assign({}, PIMAX, { exists: false, readable: false }) }), ["runtimeMissing"], ["runtime"]);
check("DLL du runtime absente", base({ activeRuntime: Object.assign({}, PIMAX, { libraryExists: false }) }), ["runtimeLib"]);
check("SteamVR actif avec Pimax Play", base({ activeRuntime: STEAM }), ["runtime", "steamvrActive"]);
check("SteamVR actif sans logiciel Pimax", base({ activeRuntime: STEAM, software: [] }), ["runtime"], ["steamvrActive"]);
check("variable XR_RUNTIME_JSON", base({ envRuntime: "D:\\rt.json" }), ["envRuntime"], ["allGood"]);
check("runtime changé depuis la dernière session", base({ activeRuntime: STEAM, lastSession: { game: "Crimson", runtime: "Pimax OpenXR 1.2", openVR: false } }), ["runtimeChanged"]);
check("même runtime que la dernière session", base({ lastSession: { game: "Crimson", runtime: "Pimax OpenXR 1.2", openVR: false } }), [], ["runtimeChanged"]);
check("dernière session OpenVR", base({ activeRuntime: STEAM, software: [], lastSession: { game: "D2R", runtime: "SteamVR 2.1", openVR: true } }), [], ["runtimeChanged"]);
check("layer absente", base({ layers: [] }), ["oursMissing"], ["allGood"]);
check("layer désactivée", base({ layers: [Object.assign({}, OURS, { enabled: false })] }), ["oursOff", "allGood"]);
check("double inscription", base({ layers: [OURS, layer("XR_APILAYER_NOVENDOR_perf_overlay", "D:\\Old\\XR_APILAYER_NOVENDOR_perf_overlay.json", { ours: true })] }), ["oursDuplicate"]);
check("inscription orpheline", base({ layers: [OURS, layer(null, "C:\\Gone\\x.json", { exists: false, readable: false })] }), ["layerOrphan"], ["layerLib"]);
check("DLL de layer absente", base({ layers: [OURS, layer("XR_APILAYER_X_y", "C:\\Y\\y.json", { libraryExists: false })] }), ["layerLib"]);
check("DLL absente mais layer désactivée", base({ layers: [OURS, layer("XR_APILAYER_X_y", "C:\\Y\\y.json", { libraryExists: false, enabled: false })] }), ["allGood"], ["layerLib"]);
check("layer coupée par variable", base({ layers: [OURS, layer("XR_APILAYER_X_y", "y.json", { disableEnvironment: "DISABLE_Y", disabledByEnvironment: true })] }), ["layerEnvOff"]);
check("layer à activer par variable", base({ layers: [OURS, layer("XR_APILAYER_X_y", "y.json", { enableEnvironment: "ENABLE_Y", enabledByEnvironment: false })] }), ["layerEnvNeeded"]);
check("même layer deux fois", base({ layers: [OURS, layer("XR_APILAYER_X_y", "C:\\A\\y.json"), layer("XR_APILAYER_X_y", "C:\\B\\y.json")] }), ["layerTwice"]);
check("OpenXR Toolkit actif", base({ layers: [OURS, layer("XR_APILAYER_MBUCCHIA_toolkit", "t.json")] }), ["toolkit"]);
check("OpenXR Toolkit désactivé", base({ layers: [OURS, layer("XR_APILAYER_MBUCCHIA_toolkit", "t.json", { enabled: false })] }), ["allGood"], ["toolkit"]);
check("Quad Views sous Pimax", base({ layers: [OURS, layer("XR_APILAYER_MBUCCHIA_quad_views_foveated", "q.json")] }), ["quadviewsPimax"]);
check("Quad Views sous SteamVR", base({ activeRuntime: STEAM, software: [], layers: [OURS, layer("XR_APILAYER_MBUCCHIA_quad_views_foveated", "q.json")] }), [], ["quadviewsPimax"]);
check("deux framegen", base({ layers: [OURS, layer("XR_APILAYER_NOVENDOR_ofxr_bridge", "o.json"), layer("XR_APILAYER_NOVENDOR_ofxr_test", "p.json")] }), ["framegenTwice"]);
check("layers de ce compte", base({ layers: [OURS, layer("XR_APILAYER_X_u", "u.json", { scope: "user" })] }), ["userLayers"]);
check("XR_ENABLE_API_LAYERS", base({ envLayers: "XR_APILAYER_LUNARG_core_validation" }), ["envLayers"]);
check("OpenComposite", base({ openVrRuntime: "C:\\Tools\\OpenComposite" }), ["opencomposite"]);
check("runtime OpenVR disparu", base({ openVrRuntimeExists: false }), ["openvrMissing"]);
check("peu de mémoire", base({ ramGb: 16 }), [], ["ramLow"]);
check("trop peu de mémoire", base({ ramGb: 8 }), ["ramLow"]);
check("peu de mémoire vidéo", base({ gpus: [{ name: "NVIDIA GeForce RTX 3060 Ti", vramGb: 7.99 }], sensorGpu: null }), [], ["vramLow"]);
check("trop peu de mémoire vidéo", base({ gpus: [{ name: "NVIDIA GeForce GTX 1660", vramGb: 6 }], sensorGpu: null }), ["vramLow"]);
check("portable hybride", base({ gpus: [{ name: "Intel(R) UHD Graphics", vramGb: 0.1 }, { name: "NVIDIA GeForce RTX 4080 Laptop GPU", vramGb: 12 }], sensorGpu: "NVIDIA GeForce RTX 4080 Laptop GPU" }), ["hybrid"]);
check("économie d'énergie", base({ powerPlanId: "A1841308-3541-4FAB-BC81-F71556F20B4A" }), ["powerSaver"]);
check("HAGS coupé (NVIDIA)", base({ hags: "off" }), ["hagsOff"]);
check("HAGS coupé (AMD)", base({ hags: "off", gpus: [{ name: "AMD Radeon RX 7900 XTX", vramGb: 24 }], sensorGpu: null }), [], ["hagsOff"]);
check("mode Jeu coupé", base({ gameMode: "off" }), ["gameModeOff"]);
check("relevé partiel", base({ errors: ["gpu", "windows"] }), ["partial"]);
check("relevé vide", { layers: [], errors: [] }, ["noRuntime", "oursMissing"]);

// Échec complet du relevé.
assert.strictEqual(C.analyze(null, fr)[0].level, "Problem");
assert.ok(C.text(null, fr, "1.7.0").includes(window.I18N.fr["cfg.f.failed"]));

// Ordre : problèmes, puis avertissements, puis informations.
const order = C.analyze(base({ activeRuntime: null, gameMode: "off", layers: [] }), fr).map(f => f.level);
assert.deepStrictEqual(order, ["Problem", "Warning", "Info"]);

// Texte copié : sections, pas de « undefined ».
const text = C.text(base({ lastSession: { game: "Crimson", runtime: "Pimax OpenXR 1.2", headset: "Pimax Dream Air", openVR: false, start: "2026-10-19T21:14:00" } }), fr, "1.7.0");
for (const word of ["CONSTATS", "CHAÎNE VR", "LAYERS OPENXR", "MATÉRIEL", "WINDOWS", "566.14", "Pimax Dream Air", "2026-10-19 21:14"]) assert.ok(text.includes(word), "texte copié sans « " + word + " »");
assert.ok(!/undefined|null|NaN/.test(text), "texte copié avec une valeur manquante :\n" + text);

// Boutons : quelles inscriptions peuvent être activées ou désactivées, et actions jointes aux constats.
assert.ok(!C.canToggle(OURS), "la layer de cette installation a son propre interrupteur");
assert.ok(C.canToggle(Object.assign(layer("XR_APILAYER_NOVENDOR_perf_overlay", "D:\\Old\\x.json", { ours: true }), { id: 3 })), "une ancienne inscription de XR Perf Overlay se désactive ici");
assert.ok(!C.canToggle(Object.assign(layer(null, "gone.json", { exists: false, readable: false }), { id: 1 })), "inscription orpheline : pas de bouton");
assert.ok(!C.canToggle(layer("XR_APILAYER_X_y", "y.json")), "sans numéro, pas de bouton");
const tk = Object.assign(layer("XR_APILAYER_MBUCCHIA_toolkit", "t.json"), { id: 4 });
const toolkitFinding = C.analyze(base({ layers: [OURS, tk] }), fr).find(f => f.key === "toolkit");
assert.deepStrictEqual(toolkitFinding.action, { id: 4, manifest: "t.json", on: false, label: "OpenXR Toolkit" });
const nolib = Object.assign(layer("XR_APILAYER_X_y", "y.json", { libraryExists: false }), { id: 2 });
assert.strictEqual(C.analyze(base({ layers: [OURS, nolib] }), fr).find(f => f.key === "layerLib").action.id, 2);
const rows = C.sections(base({ layers: [OURS, tk, Object.assign(layer("XR_APILAYER_X_z", "z.json", { enabled: false }), { id: 5 })] }), fr).find(s => s.id === "layers").rows;
assert.strictEqual(rows[0].toggle, null);
assert.deepStrictEqual(rows[1].toggle, { id: 4, manifest: "t.json", on: false, label: "OpenXR Toolkit" });
assert.strictEqual(rows[2].toggle.on, true, "layer désactivée : bouton « Réactiver »");
// Variable d'environnement : le registre dit « active », le bouton propose donc de désactiver l'inscription.
const envOff = Object.assign(layer("XR_APILAYER_X_e", "e.json", { disableEnvironment: "D", disabledByEnvironment: true }), { id: 6 });
assert.strictEqual(C.sections(base({ layers: [OURS, envOff] }), fr).find(s => s.id === "layers").rows[1].toggle.on, false);

console.log(count + " cas de configuration et la reconnaissance : OK");
