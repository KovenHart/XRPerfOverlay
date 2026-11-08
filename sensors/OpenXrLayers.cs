// Ordre des « API layers » OpenXR implicites (page Configuration ; la liste vient de ConfigProbe), tel que
// le chargeur OpenXR les lit :
//   HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit (tous les utilisateurs), puis
//   HKCU\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit (compte de l'utilisateur ; ignorée par un jeu lancé
//   en administrateur).
// Une valeur par layer : nom = chemin de son fichier .json ; une valeur DWORD de 4 octets à 0 = activée,
// autre DWORD = désactivée, autre type = ignorée par le chargeur.
// Le chargeur parcourt les valeurs dans l'ordre de la clé et place la première tout en haut de la chaîne :
// c'est elle que le jeu appelle en premier ; la dernière est la plus proche du runtime
// (src/loader/manifest_file.cpp et loader_instance.cpp du SDK OpenXR). Cet ordre est celui dans lequel les
// valeurs ont été créées : pour le changer, on supprime puis recrée les valeurs, comme le fait l'outil
// « OpenXR API Layers GUI ».
//
// Ces clés contiennent aussi les inscriptions des autres logiciels : rien ne doit s'y perdre.
//   - Les valeurs sont copiées telles quelles (type et octets bruts), quel que soit leur type.
//   - Seule la fin de la liste, à partir du premier rang qui change, est supprimée puis recréée.
//   - Avant de supprimer, la liste à recréer est écrite dans un journal (openxr-layers-en-cours.json) ;
//     si le programme s'arrête au milieu, elle est remise au démarrage suivant (RecoverPending).
//   - Avant chaque changement, l'état des deux clés est gardé (openxr-layers-avant.json) : « Annuler »
//     le remet. Les deux fichiers sont dans %LOCALAPPDATA%\XRPerfOverlay et écrits d'un bloc (fichier
//     temporaire puis remplacement).
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Text.Json;
using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;

namespace XRPerfSensors
{
    internal static class OpenXrLayers
    {
        private const string KeyPath = @"SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit";
        public const string Machine = "machine";
        public const string User = "user";
        private static readonly string[] Hives = { Machine, User };

        private const uint RegDword = 4;

        // Valeur de registre copiée telle quelle.
        private sealed class Raw
        {
            public string Name;
            public uint Type;
            public byte[] Data;

            public bool SameContent(Raw other) { return other != null && Type == other.Type && Data.SequenceEqual(other.Data); }
        }

        private static string Folder
        {
            get { return System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "XRPerfOverlay"); }
        }
        private static string BackupPath { get { return System.IO.Path.Combine(Folder, "openxr-layers-avant.json"); } }
        private static string PendingPath { get { return System.IO.Path.Combine(Folder, "openxr-layers-en-cours.json"); } }

        public static bool HasBackup { get { try { return File.Exists(BackupPath); } catch (Exception) { return false; } } }

        // --- Accès au registre ----------------------------------------------------------------------

        [DllImport("advapi32.dll", CharSet = CharSet.Unicode)]
        private static extern int RegQueryValueExW(SafeRegistryHandle key, string name, IntPtr reserved, out uint type, byte[] data, ref uint size);

        [DllImport("advapi32.dll", CharSet = CharSet.Unicode)]
        private static extern int RegSetValueExW(SafeRegistryHandle key, string name, int reserved, uint type, byte[] data, uint size);

        [DllImport("user32.dll")]
        private static extern IntPtr GetShellWindow();

        [DllImport("user32.dll")]
        private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr OpenProcess(uint access, bool inherit, uint processId);

        [DllImport("advapi32.dll", SetLastError = true)]
        private static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);

        [DllImport("kernel32.dll")]
        private static extern bool CloseHandle(IntPtr handle);

        // Compte de la session Windows (propriétaire du bureau). XRPerfSensors tourne en administrateur :
        // si l'élévation s'est faite avec un autre compte, HKCU serait celle de cet administrateur, pas
        // celle du joueur. On ouvre alors HKEY_USERS\<SID du joueur>.
        private static readonly Lazy<string> ShellUserSid = new Lazy<string>(() =>
        {
            IntPtr process = IntPtr.Zero, token = IntPtr.Zero;
            try
            {
                IntPtr shell = GetShellWindow();
                if (shell == IntPtr.Zero) return null;
                GetWindowThreadProcessId(shell, out uint pid);
                process = OpenProcess(0x1000 /* PROCESS_QUERY_LIMITED_INFORMATION */, false, pid);
                if (process == IntPtr.Zero || !OpenProcessToken(process, 0x0008 | 0x0002 /* TOKEN_QUERY | TOKEN_DUPLICATE */, out token)) return null;
                using (var identity = new WindowsIdentity(token))
                    return identity.User?.Value;
            }
            catch (Exception)
            {
                return null;
            }
            finally
            {
                if (token != IntPtr.Zero) CloseHandle(token);
                if (process != IntPtr.Zero) CloseHandle(process);
            }
        });

        private static RegistryKey Open(string hive, bool writable)
        {
            if (hive == Machine)
            {
                using (RegistryKey root = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64))
                    return root.OpenSubKey(KeyPath, writable);
            }
            string sid = ShellUserSid.Value;
            string own = null;
            try { using (WindowsIdentity me = WindowsIdentity.GetCurrent()) own = me.User?.Value; } catch (Exception) { }
            if (!string.IsNullOrEmpty(sid) && !string.Equals(sid, own, StringComparison.OrdinalIgnoreCase))
            {
                using (RegistryKey users = RegistryKey.OpenBaseKey(RegistryHive.Users, RegistryView.Registry64))
                    return users.OpenSubKey(sid + "\\" + KeyPath, writable);
            }
            using (RegistryKey root = RegistryKey.OpenBaseKey(RegistryHive.CurrentUser, RegistryView.Registry64))
                return root.OpenSubKey(KeyPath, writable);
        }

        private static Raw ReadRaw(RegistryKey key, string name)
        {
            uint size = 0;
            int status = RegQueryValueExW(key.Handle, name, IntPtr.Zero, out uint type, null, ref size);
            if (status != 0 && status != 234 /* ERROR_MORE_DATA */) return null;
            var data = new byte[size];
            status = RegQueryValueExW(key.Handle, name, IntPtr.Zero, out type, data, ref size);
            if (status != 0) return null;
            if (size < data.Length) Array.Resize(ref data, (int)size);
            return new Raw { Name = name, Type = type, Data = data };
        }

        private static void WriteRaw(RegistryKey key, Raw value)
        {
            int status = RegSetValueExW(key.Handle, value.Name, 0, value.Type, value.Data, (uint)value.Data.Length);
            if (status != 0) throw new IOException("RegSetValueEx " + status + " : " + value.Name);
        }

        // Toutes les valeurs nommées, dans l'ordre de la clé (la valeur par défaut, sans nom, n'est pas une layer).
        private static List<Raw> Snapshot(RegistryKey key)
        {
            var list = new List<Raw>();
            foreach (string name in key.GetValueNames())
            {
                if (string.IsNullOrEmpty(name)) continue;
                Raw raw = ReadRaw(key, name);
                if (raw == null) throw new IOException("Lecture impossible : " + name);
                list.Add(raw);
            }
            return list;
        }

        private static bool IsValid(Raw r) { return r.Type == RegDword && r.Data.Length == 4; }
        private static bool IsEnabled(Raw r) { return IsValid(r) && BitConverter.ToUInt32(r.Data, 0) == 0; }

        // --- Fichiers : sauvegarde et journal -------------------------------------------------------

        private static byte[] ToJson(Action<Utf8JsonWriter> body)
        {
            using (var stream = new MemoryStream())
            {
                using (var w = new Utf8JsonWriter(stream, new JsonWriterOptions { Indented = true }))
                {
                    w.WriteStartObject();
                    body(w);
                    w.WriteEndObject();
                }
                return stream.ToArray();
            }
        }

        private static void WriteValues(Utf8JsonWriter w, string property, IEnumerable<Raw> values)
        {
            w.WriteStartArray(property);
            foreach (Raw r in values)
            {
                w.WriteStartObject();
                w.WriteString("name", r.Name);
                w.WriteNumber("type", r.Type);
                w.WriteString("data", Convert.ToBase64String(r.Data));
                w.WriteEndObject();
            }
            w.WriteEndArray();
        }

        private static List<Raw> ReadValues(JsonElement root, string property)
        {
            var list = new List<Raw>();
            if (!root.TryGetProperty(property, out JsonElement array) || array.ValueKind != JsonValueKind.Array) return list;
            foreach (JsonElement e in array.EnumerateArray())
            {
                string name = e.TryGetProperty("name", out JsonElement n) ? n.GetString() : null;
                if (string.IsNullOrEmpty(name) || !e.TryGetProperty("type", out JsonElement t) || !t.TryGetUInt32(out uint type)) continue;
                byte[] data;
                try { data = Convert.FromBase64String(e.GetProperty("data").GetString() ?? ""); } catch (Exception) { continue; }
                list.Add(new Raw { Name = name, Type = type, Data = data });
            }
            return list;
        }

        // Écrit d'un bloc : fichier temporaire, puis remplacement.
        private static void WriteFile(string path, byte[] content)
        {
            Directory.CreateDirectory(Folder);
            string temp = path + ".tmp";
            File.WriteAllBytes(temp, content);
            File.Move(temp, path, true);
        }

        private static byte[] CurrentState()
        {
            var state = new Dictionary<string, List<Raw>>();
            foreach (string hive in Hives)
            {
                using (RegistryKey key = Open(hive, false))
                    state[hive] = key == null ? new List<Raw>() : Snapshot(key);
            }
            return ToJson(w => { foreach (string hive in Hives) WriteValues(w, hive, state[hive]); });
        }

        /// <summary>Garde l'état actuel des deux clés, pour « Annuler ».</summary>
        public static void BackupNow()
        {
            WriteFile(BackupPath, CurrentState());
        }

        /// <summary>
        /// Après un arrêt au milieu d'un réordonnancement : remet les valeurs du journal qui manquent.
        /// Appelé au démarrage et avant chaque lecture ou changement.
        /// </summary>
        public static void RecoverPending()
        {
            try
            {
                if (!File.Exists(PendingPath)) return;
                using (JsonDocument doc = JsonDocument.Parse(File.ReadAllText(PendingPath)))
                {
                    string hive = doc.RootElement.TryGetProperty("hive", out JsonElement h) ? h.GetString() : null;
                    if (hive == Machine || hive == User)
                    {
                        using (RegistryKey key = Open(hive, true))
                        {
                            if (key != null)
                            {
                                var present = new HashSet<string>(key.GetValueNames(), StringComparer.OrdinalIgnoreCase);
                                foreach (Raw r in ReadValues(doc.RootElement, "values"))
                                    if (!present.Contains(r.Name)) WriteRaw(key, r);
                            }
                        }
                    }
                }
                File.Delete(PendingPath);
            }
            catch (Exception)
            {
                // Journal gardé : nouvel essai à la prochaine occasion.
            }
        }

        // --- Changements ---------------------------------------------------------------------------

        private static string Denied() { return Loc.T("Accès refusé : XRPerfSensors doit être lancé en administrateur.", "Access denied: XRPerfSensors must run as administrator."); }
        private static string Gone() { return Loc.T("Cette layer n'est plus inscrite : la liste a été relue.", "This layer is no longer registered: the list was read again."); }

        /// <summary>Active ou désactive une layer. Retourne null si c'est fait (ou déjà dans cet état), sinon l'erreur.</summary>
        public static string SetEnabled(string hive, string path, bool enabled)
        {
            try
            {
                RecoverPending();
                using (RegistryKey key = Open(hive, true))
                {
                    if (key == null) return Gone();
                    Raw raw = ReadRaw(key, path);
                    if (raw == null) return Gone();
                    if (!IsValid(raw)) return Loc.T("Inscription invalide : le chargeur OpenXR l'ignore déjà.", "Invalid registration: the OpenXR loader already ignores it.");
                    if (IsEnabled(raw) == enabled) return null;
                    BackupNow();
                    // Même valeur, même place : seul son contenu change.
                    WriteRaw(key, new Raw { Name = raw.Name, Type = RegDword, Data = BitConverter.GetBytes(enabled ? 0u : 1u) });
                }
                return null;
            }
            catch (UnauthorizedAccessException) { return Denied(); }
            catch (Exception ex) { return Loc.T("Changement impossible : ", "Change failed: ") + ex.Message; }
        }

        /// <summary>
        /// Range les layers d'un emplacement dans l'ordre donné (toutes ses valeurs, chacune une fois).
        /// Retourne null si c'est fait, sinon l'erreur.
        /// </summary>
        public static string Reorder(string hive, IList<string> order)
        {
            try
            {
                RecoverPending();
                using (RegistryKey key = Open(hive, true))
                {
                    if (key == null) return Gone();
                    List<Raw> current = Snapshot(key);
                    var byName = new Dictionary<string, Raw>(StringComparer.OrdinalIgnoreCase);
                    foreach (Raw r in current) byName[r.Name] = r;
                    if (order.Count != current.Count || !order.All(byName.ContainsKey) || new HashSet<string>(order, StringComparer.OrdinalIgnoreCase).Count != order.Count)
                        return Loc.T("La liste a changé entre-temps : elle a été relue.", "The list changed in the meantime: it was read again.");
                    List<Raw> wanted = order.Select(n => byName[n]).ToList();
                    if (SameOrder(current, wanted)) return null;
                    BackupNow();
                    Apply(key, hive, current, wanted);
                }
                return null;
            }
            catch (UnauthorizedAccessException) { return Denied(); }
            catch (Exception ex) { return Loc.T("Changement impossible : ", "Change failed: ") + ex.Message; }
        }

        private static bool SameOrder(List<Raw> a, List<Raw> b)
        {
            return a.Count == b.Count && a.Zip(b, (x, y) => string.Equals(x.Name, y.Name, StringComparison.OrdinalIgnoreCase)).All(same => same);
        }

        /// <summary>
        /// Passe la clé de <paramref name="current"/> à <paramref name="wanted"/> (mêmes noms, ordre et contenu
        /// voulus). Avant le premier rang qui change, les valeurs restent en place (contenu mis à jour s'il
        /// diffère) ; à partir de ce rang, elles sont supprimées puis recréées dans l'ordre, sous la protection
        /// du journal.
        /// </summary>
        private static void Apply(RegistryKey key, string hive, List<Raw> current, List<Raw> wanted)
        {
            int first = 0;
            while (first < current.Count && string.Equals(current[first].Name, wanted[first].Name, StringComparison.OrdinalIgnoreCase)) first++;
            for (int i = 0; i < first; i++)
                if (!current[i].SameContent(wanted[i])) WriteRaw(key, wanted[i]);
            if (first == current.Count) return;

            List<Raw> tail = wanted.Skip(first).ToList();
            WriteFile(PendingPath, ToJson(w => { w.WriteString("hive", hive); WriteValues(w, "values", tail); }));
            try
            {
                for (int i = first; i < current.Count; i++) key.DeleteValue(current[i].Name, false);
                foreach (Raw r in tail) WriteRaw(key, r);
            }
            catch (Exception)
            {
                RecoverPending();
                throw;
            }
            File.Delete(PendingPath);
        }

        /// <summary>
        /// Remet l'état d'avant le dernier changement : ordre et activation des layers encore inscrites (les
        /// layers installées depuis restent à la fin, telles quelles ; celles retirées depuis ne reviennent pas).
        /// L'état actuel devient la nouvelle sauvegarde seulement si tout a réussi : annuler deux fois revient
        /// au point de départ, et un échec garde la sauvegarde intacte.
        /// </summary>
        public static string Restore()
        {
            string temp = BackupPath + ".nouveau";
            try
            {
                RecoverPending();
                if (!HasBackup) return Loc.T("Rien à annuler.", "Nothing to undo.");
                var saved = new Dictionary<string, List<Raw>>();
                using (JsonDocument doc = JsonDocument.Parse(File.ReadAllText(BackupPath)))
                    foreach (string hive in Hives) saved[hive] = ReadValues(doc.RootElement, hive);

                Directory.CreateDirectory(Folder);
                File.WriteAllBytes(temp, CurrentState());
                foreach (string hive in Hives)
                {
                    using (RegistryKey key = Open(hive, true))
                    {
                        if (key == null) continue;
                        List<Raw> current = Snapshot(key);
                        var now = new Dictionary<string, Raw>(StringComparer.OrdinalIgnoreCase);
                        foreach (Raw r in current) now[r.Name] = r;
                        var wanted = new List<Raw>();
                        var placed = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                        foreach (Raw r in saved[hive])
                            if (now.TryGetValue(r.Name, out Raw present) && placed.Add(r.Name)) wanted.Add(new Raw { Name = present.Name, Type = r.Type, Data = r.Data });
                        foreach (Raw r in current)
                            if (placed.Add(r.Name)) wanted.Add(r);
                        if (SameOrder(current, wanted) && current.Zip(wanted, (a, b) => a.SameContent(b)).All(same => same)) continue;
                        Apply(key, hive, current, wanted);
                    }
                }
                File.Move(temp, BackupPath, true);
                return null;
            }
            catch (UnauthorizedAccessException) { TryDelete(temp); return Denied(); }
            catch (Exception ex) { TryDelete(temp); return Loc.T("Annulation impossible : ", "Undo failed: ") + ex.Message; }
        }

        private static void TryDelete(string path)
        {
            try { if (File.Exists(path)) File.Delete(path); } catch (Exception) { }
        }
    }
}
