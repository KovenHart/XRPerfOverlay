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
using System.Security.Principal;
using System.Threading.Tasks;
using Microsoft.Win32;

namespace XRPerfSensors
{
    internal static class LayerRegistry
    {
        public const string ManifestName = "XR_APILAYER_NOVENDOR_perf_overlay.json";
        public const string CommandArgument = "--layer";
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
    }
}
