// Relevé de la configuration du PC pour la page « Configuration » de XRPerfSensors : chaîne VR (runtime
// OpenXR actif, layers OpenXR inscrites, runtime OpenVR), logiciels VR installés, matériel et réglages de
// Windows qui comptent pour la VR. Lecture seule : rien n'est modifié.
//
// Ce fichier ne fait que relever des faits et les envoyer en JSON à la page. Leur lecture (constats et
// conseils) est faite par l'interface, dans ui/config.js, où elle est testée hors Windows.
//
// Les chemins situés dans le dossier de l'utilisateur sont écrits « %USERPROFILE%\... » : un diagnostic
// copié et partagé ne contient pas le nom du compte Windows.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text.Json.Serialization;
using Microsoft.Win32;

namespace XRPerfSensors
{
    /// <summary>Runtime OpenXR (actif ou installé), lu dans son fichier .json.</summary>
    internal sealed class XrRuntimeInfo
    {
        public string Path;
        public bool Exists;
        /// <summary>Fichier .json lu et compris.</summary>
        public bool Readable;
        public string Name;
        public string Library;
        /// <summary>Null : impossible à dire (simple nom de fichier, cherché par Windows dans ses propres dossiers).</summary>
        public bool? LibraryExists;
    }

    /// <summary>Layer OpenXR implicite, telle qu'inscrite dans le registre et décrite par son fichier .json.</summary>
    internal sealed class XrLayerInfo
    {
        /// <summary>Numéro dans ce relevé : la page le renvoie pour activer ou désactiver cette inscription.</summary>
        public int Id;
        /// <summary>"machine" (HKLM, tous les comptes) ou "user" (HKCU, ce compte seulement).</summary>
        public string Scope;
        /// <summary>Nom exact de la valeur dans le registre (non envoyé à la page : il peut contenir le nom du compte).</summary>
        [JsonIgnore] public string RawName;
        [JsonIgnore] public bool Machine;
        public string Manifest;
        /// <summary>Valeur DWORD de l'inscription : 0 = chargée, autre = ignorée par le chargeur OpenXR.</summary>
        public bool Enabled;
        public bool Exists;
        public bool Readable;
        public string Name;
        public string Description;
        public string Library;
        public bool? LibraryExists;
        public string DisableEnvironment;
        /// <summary>La variable d'environnement de désactivation est définie pour ce compte.</summary>
        public bool DisabledByEnvironment;
        public string EnableEnvironment;
        /// <summary>Layer qui ne se charge que si sa variable d'activation est définie : l'est-elle ?</summary>
        public bool? EnabledByEnvironment;
        /// <summary>Inscription de XR Perf Overlay ; <see cref="Current"/> : celle de cette installation.</summary>
        public bool Ours;
        public bool Current;
    }

    internal sealed class SoftwareInfo
    {
        public string Name;
        public string Version;
    }

    internal sealed class GpuInfo
    {
        public string Name;
        /// <summary>Version du pilote telle que Windows la note (32.0.15.6614 pour NVIDIA 566.14).</summary>
        public string DriverVersion;
        public double? VramGb;
    }

    internal sealed class LastSessionInfo
    {
        public string Game;
        public string Runtime;
        public string Headset;
        public bool OpenVR;
        public string Start;
    }

    internal sealed class ConfigSnapshot
    {
        public string CollectedAt;
        public XrRuntimeInfo ActiveRuntime;
        /// <summary>XR_RUNTIME_JSON : remplace le runtime choisi dans le registre.</summary>
        public string EnvRuntime;
        /// <summary>XR_ENABLE_API_LAYERS : layers explicites ajoutées à tous les jeux.</summary>
        public string EnvLayers;
        public List<XrRuntimeInfo> AvailableRuntimes = new List<XrRuntimeInfo>();
        /// <summary>Dans l'ordre de lecture : machine (HKLM) puis utilisateur (HKCU), chacune dans l'ordre du registre.</summary>
        public List<XrLayerInfo> Layers = new List<XrLayerInfo>();
        public string OpenVrRuntime;
        public bool? OpenVrRuntimeExists;
        public List<SoftwareInfo> Software = new List<SoftwareInfo>();
        public string Cpu;
        public int Threads;
        public double? RamGb;
        public List<GpuInfo> Gpus = new List<GpuInfo>();
        /// <summary>Carte suivie par les capteurs (la dédiée, s'il y en a plusieurs).</summary>
        public string SensorGpu;
        public string Windows;
        public string WindowsVersion;
        public string WindowsBuild;
        /// <summary>Planification GPU accélérée par le matériel : "on", "off", ou null (réglage par défaut).</summary>
        public string Hags;
        /// <summary>Mode Jeu : "on", "off", ou "default" (jamais touché : activé).</summary>
        public string GameMode;
        public string PowerPlanId;
        public string PowerPlanName;
        public LastSessionInfo LastSession;
        /// <summary>Parties du relevé qui ont échoué (registre inaccessible...).</summary>
        public List<string> Errors = new List<string>();
    }

    internal static class ConfigProbe
    {
        private const string OpenXrKey = @"SOFTWARE\Khronos\OpenXR\1";

        // Logiciels VR reconnus dans la liste des programmes installés (nom affiché, sans tenir compte de la casse).
        private static readonly string[] SoftwareNames =
        {
            "Pimax", "PiTool", "SteamVR", "OpenXR", "OpenComposite", "Virtual Desktop", "Oculus", "Meta Quest", "ReShade",
            "Ultraleap", "OpenKneeboard", "NeckSafer", "MotionCompensation", "Motion Compensation", "UEVR", "VRPerfKit",
        };

        private static readonly JsonSerializerOptions Options = new JsonSerializerOptions
        {
            IncludeFields = true,
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        };

        private static readonly JsonDocumentOptions Lenient = new JsonDocumentOptions
        {
            AllowTrailingCommas = true,
            CommentHandling = JsonCommentHandling.Skip,
        };

        /// <summary>Message « config » pour la page.</summary>
        public static string Message(ConfigSnapshot snapshot)
        {
            return JsonSerializer.Serialize(new Dictionary<string, object> { { "type", "config" }, { "config", snapshot } }, Options);
        }

        /// <summary>
        /// Relève la configuration. <paramref name="ourManifest"/> : fichier .json de la layer de cette
        /// installation ; <paramref name="sensorGpu"/> : carte suivie par les capteurs (ou null).
        /// </summary>
        public static ConfigSnapshot Collect(string ourManifest, string sensorGpu, SessionReport last)
        {
            var s = new ConfigSnapshot
            {
                CollectedAt = DateTime.Now.ToString("s", CultureInfo.InvariantCulture),
                SensorGpu = sensorGpu,
            };
            Step(s, "openxr", () => ReadOpenXr(s, ourManifest));
            Step(s, "openvr", () => ReadOpenVr(s));
            Step(s, "software", () => ReadSoftware(s));
            Step(s, "cpu", () => ReadCpu(s));
            Step(s, "ram", () => s.RamGb = RamGb());
            Step(s, "gpu", () => ReadGpus(s));
            Step(s, "windows", () => ReadWindows(s));
            if (last != null && last.Readable)
            {
                s.LastSession = new LastSessionInfo
                {
                    Game = last.Game,
                    Runtime = last.Runtime,
                    Headset = last.Headset,
                    OpenVR = last.OpenVR,
                    Start = (last.Start ?? last.FileTime).ToString("s", CultureInfo.InvariantCulture),
                };
            }
            return s;
        }

        private static void Step(ConfigSnapshot s, string part, Action action)
        {
            try
            {
                action();
            }
            catch (Exception)
            {
                s.Errors.Add(part);
            }
        }

        // --- Outils -----------------------------------------------------------------------------------

        private static RegistryKey Open(RegistryHive hive, string path, RegistryView view = RegistryView.Registry64)
        {
            try
            {
                using (RegistryKey root = RegistryKey.OpenBaseKey(hive, view))
                    return root.OpenSubKey(path, false);
            }
            catch (Exception)
            {
                return null;
            }
        }

        private static string Text(RegistryKey key, string name)
        {
            try
            {
                object value = key?.GetValue(name);
                if (value is string text) return text.Trim();
                if (value is string[] lines) return string.Join(" ", lines).Trim();
                return null;
            }
            catch (Exception)
            {
                return null;
            }
        }

        private static long? Integer(RegistryKey key, string name)
        {
            try
            {
                object value = key?.GetValue(name);
                switch (value)
                {
                    case int i: return unchecked((uint)i);
                    case long l: return l;
                    case byte[] bytes when bytes.Length >= 8: return BitConverter.ToInt64(bytes, 0);
                    case byte[] bytes when bytes.Length >= 4: return BitConverter.ToUInt32(bytes, 0);
                    default: return null;
                }
            }
            catch (Exception)
            {
                return null;
            }
        }

        private static bool FileThere(string path)
        {
            try
            {
                return !string.IsNullOrEmpty(path) && File.Exists(path);
            }
            catch (Exception)
            {
                return false;
            }
        }

        private static readonly string UserProfile = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);

        /// <summary>Chemin à montrer : le dossier de l'utilisateur remplacé par %USERPROFILE%.</summary>
        private static string Tidy(string path)
        {
            if (string.IsNullOrEmpty(path) || string.IsNullOrEmpty(UserProfile)) return path;
            string profile = UserProfile.TrimEnd('\\', '/');
            if (path.Length >= profile.Length && path.StartsWith(profile, StringComparison.OrdinalIgnoreCase) &&
                (path.Length == profile.Length || path[profile.Length] == '\\' || path[profile.Length] == '/'))
                return "%USERPROFILE%" + path.Substring(profile.Length);
            return path;
        }

        private static string Str(JsonElement e, string name)
        {
            return e.ValueKind == JsonValueKind.Object && e.TryGetProperty(name, out JsonElement v) && v.ValueKind == JsonValueKind.String ? v.GetString() : null;
        }

        /// <summary>
        /// Chemin de la DLL d'un fichier .json OpenXR : relatif au dossier du .json s'il contient un dossier
        /// (".\x.dll", "bin\x.dll"), absolu tel quel. Un simple nom de fichier est cherché par Windows dans ses
        /// propres dossiers : on regarde à côté du .json, et sinon on ne conclut pas.
        /// </summary>
        private static void Library(string manifest, string library, out string path, out bool? exists)
        {
            path = null;
            exists = null;
            if (string.IsNullOrWhiteSpace(library)) return;
            string folder = Path.GetDirectoryName(manifest) ?? "";
            bool bare = library.IndexOf('\\') < 0 && library.IndexOf('/') < 0;
            string full;
            try
            {
                full = Path.IsPathRooted(library) ? library : Path.GetFullPath(Path.Combine(folder, library));
            }
            catch (Exception)
            {
                path = library;
                return;
            }
            path = Tidy(full);
            if (FileThere(full)) exists = true;
            else if (!bare) exists = false;
            else path = library;
        }

        private static JsonDocument ReadJson(string path)
        {
            return JsonDocument.Parse(File.ReadAllText(path), Lenient);
        }

        // --- OpenXR -----------------------------------------------------------------------------------

        private static void ReadOpenXr(ConfigSnapshot s, string ourManifest)
        {
            s.EnvRuntime = Tidy(Environment.GetEnvironmentVariable("XR_RUNTIME_JSON"));
            s.EnvLayers = Environment.GetEnvironmentVariable("XR_ENABLE_API_LAYERS");

            using (RegistryKey key = Open(RegistryHive.LocalMachine, OpenXrKey))
            {
                string active = Text(key, "ActiveRuntime");
                if (!string.IsNullOrEmpty(active)) s.ActiveRuntime = ReadRuntime(Environment.ExpandEnvironmentVariables(active));
            }
            using (RegistryKey key = Open(RegistryHive.LocalMachine, OpenXrKey + @"\AvailableRuntimes"))
            {
                if (key != null)
                    foreach (string name in key.GetValueNames())
                        if (!string.IsNullOrEmpty(name)) s.AvailableRuntimes.Add(ReadRuntime(Environment.ExpandEnvironmentVariables(name)));
            }
            ReadLayers(s, RegistryHive.LocalMachine, "machine", ourManifest);
            ReadLayers(s, RegistryHive.CurrentUser, "user", ourManifest);
        }

        private static XrRuntimeInfo ReadRuntime(string path)
        {
            var info = new XrRuntimeInfo { Path = Tidy(path), Exists = FileThere(path) };
            if (!info.Exists) return info;
            try
            {
                using (JsonDocument doc = ReadJson(path))
                {
                    if (doc.RootElement.ValueKind == JsonValueKind.Object && doc.RootElement.TryGetProperty("runtime", out JsonElement runtime) &&
                        runtime.ValueKind == JsonValueKind.Object)
                    {
                        info.Readable = true;
                        info.Name = Str(runtime, "name");
                        Library(path, Str(runtime, "library_path"), out info.Library, out info.LibraryExists);
                    }
                }
            }
            catch (Exception)
            {
                // .json illisible : Readable reste faux.
            }
            return info;
        }

        private static void ReadLayers(ConfigSnapshot s, RegistryHive hive, string scope, string ourManifest)
        {
            using (RegistryKey key = Open(hive, OpenXrKey + @"\ApiLayers\Implicit"))
            {
                if (key == null) return;
                foreach (string name in key.GetValueNames())
                {
                    if (string.IsNullOrEmpty(name)) continue;
                    string manifest = Environment.ExpandEnvironmentVariables(name);
                    long? value = Integer(key, name);
                    var layer = new XrLayerInfo
                    {
                        Id = s.Layers.Count,
                        Scope = scope,
                        RawName = name,
                        Machine = hive == RegistryHive.LocalMachine,
                        Manifest = Tidy(manifest),
                        Enabled = value.HasValue && value.Value == 0,
                        Exists = FileThere(manifest),
                        Ours = manifest.EndsWith(LayerRegistry.ManifestName, StringComparison.OrdinalIgnoreCase),
                    };
                    layer.Current = layer.Ours && !string.IsNullOrEmpty(ourManifest) && string.Equals(manifest, ourManifest, StringComparison.OrdinalIgnoreCase);
                    if (layer.Exists) ReadLayerManifest(layer, manifest);
                    s.Layers.Add(layer);
                }
            }
        }

        private static void ReadLayerManifest(XrLayerInfo layer, string manifest)
        {
            try
            {
                using (JsonDocument doc = ReadJson(manifest))
                {
                    if (doc.RootElement.ValueKind != JsonValueKind.Object || !doc.RootElement.TryGetProperty("api_layer", out JsonElement api) ||
                        api.ValueKind != JsonValueKind.Object)
                        return;
                    layer.Readable = true;
                    layer.Name = Str(api, "name");
                    layer.Description = Str(api, "description");
                    Library(manifest, Str(api, "library_path"), out layer.Library, out layer.LibraryExists);
                    layer.DisableEnvironment = Str(api, "disable_environment");
                    if (!string.IsNullOrEmpty(layer.DisableEnvironment))
                        layer.DisabledByEnvironment = Environment.GetEnvironmentVariable(layer.DisableEnvironment) != null;
                    layer.EnableEnvironment = Str(api, "enable_environment");
                    if (!string.IsNullOrEmpty(layer.EnableEnvironment))
                        layer.EnabledByEnvironment = Environment.GetEnvironmentVariable(layer.EnableEnvironment) != null;
                }
            }
            catch (Exception)
            {
                // .json illisible : Readable reste faux.
            }
        }

        // --- OpenVR (jeux SteamVR) ----------------------------------------------------------------------

        // Le runtime OpenVR utilisé est le premier du fichier openvrpaths.vrpath : SteamVR, ou OpenComposite
        // s'il a été installé pour tout le système.
        private static void ReadOpenVr(ConfigSnapshot s)
        {
            string local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            string file = Path.Combine(local, "openvr", "openvrpaths.vrpath");
            if (!FileThere(file)) return;
            using (JsonDocument doc = ReadJson(file))
            {
                if (doc.RootElement.ValueKind == JsonValueKind.Object && doc.RootElement.TryGetProperty("runtime", out JsonElement runtimes) &&
                    runtimes.ValueKind == JsonValueKind.Array)
                {
                    foreach (JsonElement runtime in runtimes.EnumerateArray())
                    {
                        if (runtime.ValueKind != JsonValueKind.String) continue;
                        string path = runtime.GetString();
                        s.OpenVrRuntime = Tidy(path);
                        try
                        {
                            s.OpenVrRuntimeExists = Directory.Exists(path);
                        }
                        catch (Exception)
                        {
                            s.OpenVrRuntimeExists = null;
                        }
                        break;
                    }
                }
            }
        }

        // --- Logiciels installés ------------------------------------------------------------------------

        private static void ReadSoftware(ConfigSnapshot s)
        {
            const string uninstall = @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall";
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            var sources = new[]
            {
                Tuple.Create(RegistryHive.LocalMachine, RegistryView.Registry64),
                Tuple.Create(RegistryHive.LocalMachine, RegistryView.Registry32),
                Tuple.Create(RegistryHive.CurrentUser, RegistryView.Registry64),
            };
            foreach (var source in sources)
            {
                using (RegistryKey key = Open(source.Item1, uninstall, source.Item2))
                {
                    if (key == null) continue;
                    foreach (string sub in key.GetSubKeyNames())
                    {
                        using (RegistryKey entry = key.OpenSubKey(sub, false))
                        {
                            string name = Text(entry, "DisplayName");
                            if (string.IsNullOrEmpty(name) || !SoftwareNames.Any(n => name.IndexOf(n, StringComparison.OrdinalIgnoreCase) >= 0)) continue;
                            string version = Text(entry, "DisplayVersion") ?? "";
                            if (!seen.Add(name + "|" + version)) continue;
                            s.Software.Add(new SoftwareInfo { Name = name, Version = version });
                        }
                    }
                }
            }
            s.Software.Sort((a, b) => string.Compare(a.Name, b.Name, StringComparison.OrdinalIgnoreCase));
            if (s.Software.Count > 30) s.Software.RemoveRange(30, s.Software.Count - 30);
        }

        // --- Matériel -----------------------------------------------------------------------------------

        private static void ReadCpu(ConfigSnapshot s)
        {
            s.Threads = Environment.ProcessorCount;
            using (RegistryKey key = Open(RegistryHive.LocalMachine, @"HARDWARE\DESCRIPTION\System\CentralProcessor\0"))
                s.Cpu = Text(key, "ProcessorNameString");
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct MemoryStatusEx
        {
            public uint Length;
            public uint MemoryLoad;
            public ulong TotalPhys;
            public ulong AvailPhys;
            public ulong TotalPageFile;
            public ulong AvailPageFile;
            public ulong TotalVirtual;
            public ulong AvailVirtual;
            public ulong AvailExtendedVirtual;
        }

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GlobalMemoryStatusEx(ref MemoryStatusEx status);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetPhysicallyInstalledSystemMemory(out ulong kilobytes);

        /// <summary>Mémoire vive installée (Go), sinon celle que Windows peut utiliser.</summary>
        private static double? RamGb()
        {
            if (GetPhysicallyInstalledSystemMemory(out ulong kilobytes) && kilobytes > 0)
                return Math.Round(kilobytes / (1024.0 * 1024.0), 1);
            var status = new MemoryStatusEx { Length = (uint)Marshal.SizeOf<MemoryStatusEx>() };
            if (GlobalMemoryStatusEx(ref status) && status.TotalPhys > 0)
                return Math.Round(status.TotalPhys / (1024.0 * 1024.0 * 1024.0), 1);
            return null;
        }

        // Cartes graphiques présentes : HKLM\HARDWARE\DEVICEMAP\VIDEO, refait par Windows à chaque démarrage,
        // pointe vers la clé de chaque carte active (une entrée par sortie : plusieurs peuvent viser la même
        // carte). La liste des pilotes (Control\Class) garde aussi d'anciennes cartes retirées : elle ne sert
        // qu'à retrouver la version du pilote si la clé de la carte ne la donne pas.
        private const string DisplayClass = @"SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}";

        private static void ReadGpus(ConfigSnapshot s)
        {
            var keys = new List<string>();
            using (RegistryKey map = Open(RegistryHive.LocalMachine, @"HARDWARE\DEVICEMAP\VIDEO"))
            {
                if (map == null) return;
                foreach (string name in map.GetValueNames())
                {
                    if (!name.StartsWith(@"\Device\Video", StringComparison.OrdinalIgnoreCase)) continue;
                    string target = Text(map, name);
                    const string prefix = @"\Registry\Machine\";
                    if (string.IsNullOrEmpty(target) || !target.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) continue;
                    string path = target.Substring(prefix.Length);
                    if (!keys.Contains(path, StringComparer.OrdinalIgnoreCase)) keys.Add(path);
                }
            }
            var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (string path in keys)
            {
                using (RegistryKey key = Open(RegistryHive.LocalMachine, path))
                {
                    if (key == null) continue;
                    string name = Text(key, "DriverDesc") ?? Text(key, "Device Description");
                    if (string.IsNullOrEmpty(name)) continue;
                    // Pilote d'affichage de base de Windows (pas de vrai pilote) : sans intérêt ici.
                    if (name.StartsWith("Microsoft Basic", StringComparison.OrdinalIgnoreCase)) continue;
                    if (!names.Add(name)) continue;
                    // qwMemorySize : sur 64 bits. MemorySize, plus ancien, tient sur 32 bits : plafonné ou faux au-delà
                    // de 4 Go, il n'est retenu qu'en dessous.
                    long? memory = Integer(key, "HardwareInformation.qwMemorySize");
                    if (!memory.HasValue)
                    {
                        long? small = Integer(key, "HardwareInformation.MemorySize");
                        if (small.HasValue && small.Value < 0xFFF00000L) memory = small;
                    }
                    s.Gpus.Add(new GpuInfo
                    {
                        Name = name,
                        DriverVersion = Text(key, "DriverVersion") ?? DriverVersionOf(name),
                        VramGb = memory.HasValue && memory.Value > 0 ? Math.Round(memory.Value / (1024.0 * 1024.0 * 1024.0), 1) : (double?)null,
                    });
                }
            }
        }

        private static string DriverVersionOf(string name)
        {
            using (RegistryKey cls = Open(RegistryHive.LocalMachine, DisplayClass))
            {
                if (cls == null) return null;
                foreach (string sub in cls.GetSubKeyNames())
                {
                    if (sub.Length != 4 || !sub.All(char.IsDigit)) continue;
                    try
                    {
                        using (RegistryKey entry = cls.OpenSubKey(sub, false))
                            if (string.Equals(Text(entry, "DriverDesc"), name, StringComparison.OrdinalIgnoreCase)) return Text(entry, "DriverVersion");
                    }
                    catch (Exception)
                    {
                        // entrée protégée : suivante
                    }
                }
            }
            return null;
        }

        // --- Windows ------------------------------------------------------------------------------------

        private static void ReadWindows(ConfigSnapshot s)
        {
            using (RegistryKey key = Open(RegistryHive.LocalMachine, @"SOFTWARE\Microsoft\Windows NT\CurrentVersion"))
            {
                string product = Text(key, "ProductName") ?? "Windows";
                string build = Text(key, "CurrentBuildNumber") ?? Text(key, "CurrentBuild");
                long? ubr = Integer(key, "UBR");
                // Windows 11 garde « Windows 10 » dans ProductName : seul le numéro de version les distingue.
                if (int.TryParse(build, NumberStyles.Integer, CultureInfo.InvariantCulture, out int number) && number >= 22000)
                    product = product.Replace("Windows 10", "Windows 11");
                s.Windows = product;
                s.WindowsVersion = Text(key, "DisplayVersion") ?? Text(key, "ReleaseId");
                s.WindowsBuild = build == null ? null : ubr.HasValue ? build + "." + ubr.Value.ToString(CultureInfo.InvariantCulture) : build;
            }
            using (RegistryKey key = Open(RegistryHive.LocalMachine, @"SYSTEM\CurrentControlSet\Control\GraphicsDrivers"))
            {
                long? mode = Integer(key, "HwSchMode");
                s.Hags = mode == 2 ? "on" : mode == 1 ? "off" : null;
            }
            using (RegistryKey key = Open(RegistryHive.CurrentUser, @"Software\Microsoft\GameBar"))
            {
                long? auto = Integer(key, "AutoGameModeEnabled");
                s.GameMode = auto == 0 ? "off" : auto.HasValue ? "on" : "default";
            }
            using (RegistryKey key = Open(RegistryHive.LocalMachine, @"SYSTEM\CurrentControlSet\Control\Power\User\PowerSchemes"))
            {
                string id = Text(key, "ActivePowerScheme");
                if (!string.IsNullOrEmpty(id))
                {
                    s.PowerPlanId = id.Trim('{', '}').ToLowerInvariant();
                    using (RegistryKey plan = key.OpenSubKey(id, false))
                    {
                        // Nom des modes de Windows : "@%SystemRoot%\system32\powrprof.dll,-14,Balanced" (le texte
                        // final est le nom par défaut, en anglais) ; nom d'un mode créé par l'utilisateur : en clair.
                        string name = Text(plan, "FriendlyName");
                        if (!string.IsNullOrEmpty(name) && name.StartsWith("@", StringComparison.Ordinal))
                        {
                            int comma = name.LastIndexOf(',');
                            name = comma >= 0 && comma < name.Length - 1 && !name.Substring(comma + 1).StartsWith("-", StringComparison.Ordinal)
                                ? name.Substring(comma + 1).Trim() : null;
                        }
                        s.PowerPlanName = name;
                    }
                }
            }
        }
    }
}
