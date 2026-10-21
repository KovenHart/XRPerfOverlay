// Activation de la layer OpenXR : la clé standard des « API layers » implicites
// (HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit) contient une valeur par layer, nommée par le
// chemin de son fichier .json. DWORD 0 : la layer est chargée dans les jeux OpenXR ; 1 : le chargeur
// OpenXR l'ignore, elle n'entre plus du tout dans les jeux. Désactiver ne retire pas l'inscription :
// réactiver ne demande pas de réinstaller.
//
// L'écriture dans HKLM demande les droits administrateur. XRPerfSensors les a déjà (son manifeste
// demande l'élévation au lancement, pour les capteurs) ; s'il en était privé, il se relancerait lui-même,
// élevé, avec l'argument « --layer on » ou « --layer off » : cette instance fait la seule écriture et
// se termine aussitôt.
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Principal;
using System.Text;
using System.Threading.Tasks;
using Microsoft.Win32;

namespace XRPerfSensors
{
    internal static class LayerRegistry
    {
        public const string ManifestName = "XR_APILAYER_NOVENDOR_perf_overlay.json";
        public const string CommandArgument = "--layer";
        /// <summary>Instance relancée en administrateur pour une autre layer : « --xrlayer on|off &lt;nom en base64&gt; ».</summary>
        public const string OtherArgument = "--xrlayer";
        private const string KeyPath = @"SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit";

        public enum State { Missing, Enabled, Disabled }

        /// <summary>Résultat d'une demande de changement.</summary>
        public enum Outcome { Done, Cancelled, Missing, Failed }

        /// <summary>
        /// État de l'inscription de cette layer. Plusieurs inscriptions (anciens dossiers) : celle de
        /// <paramref name="manifest"/> si elle existe, sinon la première trouvée.
        /// </summary>
        public static State Read(string manifest)
        {
            try
            {
                using (RegistryKey hklm = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64))
                using (RegistryKey key = hklm.OpenSubKey(KeyPath, false))
                {
                    if (key == null) return State.Missing;
                    string chosen = null;
                    foreach (string name in key.GetValueNames())
                    {
                        if (!IsOurs(name)) continue;
                        if (chosen == null || string.Equals(name, manifest, StringComparison.OrdinalIgnoreCase)) chosen = name;
                    }
                    if (chosen == null) return State.Missing;
                    object value = key.GetValue(chosen);
                    return value is int number && number == 0 ? State.Enabled : State.Disabled;
                }
            }
            catch (Exception)
            {
                return State.Missing;
            }
        }

        private static bool IsOurs(string valueName)
        {
            return valueName != null && valueName.EndsWith(ManifestName, StringComparison.OrdinalIgnoreCase);
        }

        public static bool IsElevated()
        {
            try
            {
                using (WindowsIdentity identity = WindowsIdentity.GetCurrent())
                    return new WindowsPrincipal(identity).IsInRole(WindowsBuiltInRole.Administrator);
            }
            catch (Exception)
            {
                return false;
            }
        }

        /// <summary>
        /// Écrit l'état voulu dans toutes les inscriptions de cette layer (droits administrateur requis).
        /// Retourne false s'il n'y en a aucune.
        /// </summary>
        private static bool Write(bool enabled)
        {
            using (RegistryKey hklm = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64))
            using (RegistryKey key = hklm.OpenSubKey(KeyPath, true))
            {
                if (key == null) return false;
                bool found = false;
                foreach (string name in key.GetValueNames())
                {
                    if (!IsOurs(name)) continue;
                    key.SetValue(name, enabled ? 0 : 1, RegistryValueKind.DWord);
                    found = true;
                }
                return found;
            }
        }

        /// <summary>
        /// Instance relancée en administrateur (« --layer on|off ») : fait l'écriture et retourne le code
        /// de sortie (0 : fait, 2 : layer non inscrite, 1 : échec).
        /// </summary>
        public static int RunCommand(string value)
        {
            bool enabled;
            if (string.Equals(value, "on", StringComparison.OrdinalIgnoreCase)) enabled = true;
            else if (string.Equals(value, "off", StringComparison.OrdinalIgnoreCase)) enabled = false;
            else return 1;
            try
            {
                return Write(enabled) ? 0 : 2;
            }
            catch (Exception)
            {
                return 1;
            }
        }

        /// <summary>
        /// Active ou désactive la layer. Sans droits administrateur, Windows demande l'autorisation (invite
        /// UAC) ; la tâche se termine quand l'écriture est faite, refusée ou impossible.
        /// </summary>
        public static Task<Outcome> SetAsync(bool enabled, string manifest)
        {
            if (Read(manifest) == State.Missing) return Task.FromResult(Outcome.Missing);
            if (IsElevated())
            {
                try
                {
                    return Task.FromResult(Write(enabled) ? Outcome.Done : Outcome.Missing);
                }
                catch (Exception)
                {
                    return Task.FromResult(Outcome.Failed);
                }
            }
            string exe = Environment.ProcessPath;
            if (string.IsNullOrEmpty(exe) || !File.Exists(exe)) return Task.FromResult(Outcome.Failed);
            // Hors du fil de l'interface : l'invite de Windows reste ouverte le temps que l'utilisateur réponde.
            return Task.Run(() =>
            {
                Process process;
                try
                {
                    process = Process.Start(new ProcessStartInfo(exe, CommandArgument + " " + (enabled ? "on" : "off"))
                    {
                        UseShellExecute = true,
                        Verb = "runas",
                        WindowStyle = ProcessWindowStyle.Hidden,
                    });
                }
                catch (Win32Exception ex) when (ex.NativeErrorCode == 1223) // ERROR_CANCELLED : invite refusée
                {
                    return Outcome.Cancelled;
                }
                catch (Exception)
                {
                    return Outcome.Failed;
                }
                if (process == null) return Outcome.Failed;
                using (process)
                {
                    if (!process.WaitForExit(30000)) return Outcome.Failed;
                    switch (process.ExitCode)
                    {
                        case 0: return Outcome.Done;
                        case 2: return Outcome.Missing;
                        default: return Outcome.Failed;
                    }
                }
            });
        }
        // --- Autres layers (page Configuration) -------------------------------------------------------
        //
        // Même mécanisme pour n'importe quelle layer inscrite : DWORD 0 = chargée, 1 = ignorée. Seule une
        // valeur qui existe déjà est modifiée (jamais créée ni supprimée) : le choix reste réversible, et rien
        // n'est inscrit à la place d'un installateur. Les inscriptions de HKCU (ce compte) se modifient sans
        // droits administrateur ; celles de HKLM en demandent.

        private static bool WriteOther(RegistryHive hive, string valueName, bool enabled)
        {
            using (RegistryKey root = RegistryKey.OpenBaseKey(hive, RegistryView.Registry64))
            using (RegistryKey key = root.OpenSubKey(KeyPath, true))
            {
                if (key == null || !key.GetValueNames().Contains(valueName, StringComparer.Ordinal)) return false;
                // Une inscription OpenXR est un DWORD : une valeur d'un autre type n'est pas touchée.
                if (key.GetValueKind(valueName) != RegistryValueKind.DWord) return false;
                key.SetValue(valueName, enabled ? 0 : 1, RegistryValueKind.DWord);
                return true;
            }
        }

        /// <summary>
        /// Instance relancée en administrateur (« --xrlayer on|off &lt;nom&gt; », nom de la valeur de HKLM en
        /// base64) : fait l'écriture et retourne le code de sortie (0 : fait, 2 : inscription absente, 1 : échec).
        /// </summary>
        public static int RunOtherCommand(string value, string encodedName)
        {
            bool enabled;
            if (string.Equals(value, "on", StringComparison.OrdinalIgnoreCase)) enabled = true;
            else if (string.Equals(value, "off", StringComparison.OrdinalIgnoreCase)) enabled = false;
            else return 1;
            try
            {
                string name = Encoding.UTF8.GetString(Convert.FromBase64String(encodedName ?? ""));
                if (name.Length == 0) return 1;
                return WriteOther(RegistryHive.LocalMachine, name, enabled) ? 0 : 2;
            }
            catch (Exception)
            {
                return 1;
            }
        }

        /// <summary>
        /// Active ou désactive l'inscription <paramref name="valueName"/> (nom exact de la valeur, chemin du
        /// .json) dans HKLM (<paramref name="machine"/>) ou HKCU. Pour HKLM sans droits administrateur, Windows
        /// demande l'autorisation.
        /// </summary>
        public static Task<Outcome> SetOtherAsync(bool machine, string valueName, bool enabled)
        {
            if (string.IsNullOrEmpty(valueName)) return Task.FromResult(Outcome.Missing);
            if (!machine || IsElevated())
            {
                try
                {
                    return Task.FromResult(WriteOther(machine ? RegistryHive.LocalMachine : RegistryHive.CurrentUser, valueName, enabled) ? Outcome.Done : Outcome.Missing);
                }
                catch (Exception)
                {
                    return Task.FromResult(Outcome.Failed);
                }
            }
            string exe = Environment.ProcessPath;
            if (string.IsNullOrEmpty(exe) || !File.Exists(exe)) return Task.FromResult(Outcome.Failed);
            string encoded = Convert.ToBase64String(Encoding.UTF8.GetBytes(valueName));
            return Task.Run(() =>
            {
                Process process;
                try
                {
                    process = Process.Start(new ProcessStartInfo(exe, OtherArgument + " " + (enabled ? "on" : "off") + " " + encoded)
                    {
                        UseShellExecute = true,
                        Verb = "runas",
                        WindowStyle = ProcessWindowStyle.Hidden,
                    });
                }
                catch (Win32Exception ex) when (ex.NativeErrorCode == 1223) // ERROR_CANCELLED : invite refusée
                {
                    return Outcome.Cancelled;
                }
                catch (Exception)
                {
                    return Outcome.Failed;
                }
                if (process == null) return Outcome.Failed;
                using (process)
                {
                    if (!process.WaitForExit(30000)) return Outcome.Failed;
                    switch (process.ExitCode)
                    {
                        case 0: return Outcome.Done;
                        case 2: return Outcome.Missing;
                        default: return Outcome.Failed;
                    }
                }
            });
        }
    }
}
