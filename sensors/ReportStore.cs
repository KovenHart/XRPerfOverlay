// Rapports de session (.txt écrits par la layer et XRPerfVR) : lecture des dossiers, mémoire des
// rapports déjà lus, session précédente d'un même jeu, ouverture et suppression.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;

namespace XRPerfSensors
{
    internal sealed class ReportStore
    {
        private readonly Func<IList<string>> _folders; // dossiers à parcourir, recalculés à chaque actualisation
        private readonly Dictionary<string, SessionReport> _cache = new Dictionary<string, SessionReport>(StringComparer.OrdinalIgnoreCase);
        private string _signature; // dossiers et fichiers vus à la dernière actualisation (nom, date, taille)

        public ReportStore(Func<IList<string>> folders)
        {
            _folders = folders;
        }

        /// <summary>Rapports, le plus récent d'abord.</summary>
        public List<SessionReport> Reports { get; private set; } = new List<SessionReport>();

        /// <summary>Dossiers parcourus lors de la dernière actualisation.</summary>
        public IList<string> Folders { get; private set; } = new List<string>();

        /// <summary>
        /// Relit les dossiers. Retourne true si la liste a changé. Sans <paramref name="force"/>, rien n'est
        /// relu si aucun fichier n'a changé ; les rapports déjà lus et non modifiés depuis ne sont jamais relus.
        /// </summary>
        public bool Refresh(bool force)
        {
            IList<string> folders = _folders();
            var files = new List<FileInfo>();
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (string folder in folders)
            {
                try
                {
                    if (!Directory.Exists(folder)) continue;
                    foreach (FileInfo file in new DirectoryInfo(folder).GetFiles("*.txt"))
                        if (seen.Add(file.FullName)) files.Add(file);
                }
                catch (Exception)
                {
                    // dossier devenu inaccessible : ignoré
                }
            }
            string signature = string.Join("|", folders) + "#" +
                string.Join("|", files.Select(f => f.FullName + "*" + f.LastWriteTimeUtc.Ticks.ToString(CultureInfo.InvariantCulture) + "*" +
                                                   f.Length.ToString(CultureInfo.InvariantCulture)));
            if (!force && signature == _signature) return false;
            _signature = signature;
            Folders = folders;

            var reports = new List<SessionReport>();
            foreach (FileInfo file in files)
            {
                string path = file.FullName;
                if (!_cache.TryGetValue(path, out SessionReport report) || report.FileTime != file.LastWriteTime)
                {
                    report = SessionReport.Load(path);
                    // Fichier verrouillé ou disparu entre-temps : ni affiché ni mémorisé, il sera relu la prochaine fois.
                    if (report.LoadFailed)
                    {
                        _cache.Remove(path);
                        _signature = null;
                        continue;
                    }
                    _cache[path] = report;
                }
                reports.Add(report);
            }
            foreach (string gone in _cache.Keys.Where(k => !seen.Contains(k)).ToList()) _cache.Remove(gone);
            Reports = reports.OrderByDescending(r => r.Start ?? r.FileTime).ToList();
            return true;
        }

        public SessionReport Find(string path)
        {
            return string.IsNullOrEmpty(path) ? null : Reports.FirstOrDefault(r => string.Equals(r.Path, path, StringComparison.OrdinalIgnoreCase));
        }

        /// <summary>Session précédente du même jeu (la plus récente commencée avant celle-ci).</summary>
        public SessionReport PreviousOf(SessionReport report)
        {
            if (report == null) return null;
            DateTime when = report.Start ?? report.FileTime;
            return Reports
                .Where(r => r != report && r.Readable && string.Equals(r.Game, report.Game, StringComparison.OrdinalIgnoreCase) && (r.Start ?? r.FileTime) < when)
                .OrderByDescending(r => r.Start ?? r.FileTime)
                .FirstOrDefault();
        }

        /// <summary>Ouvre des rapports dans le Bloc-notes (cinq au plus). Retourne un message d'erreur, ou null.</summary>
        public static string OpenInNotepad(IEnumerable<SessionReport> reports)
        {
            foreach (SessionReport r in reports.Take(5))
            {
                try
                {
                    Process.Start(new ProcessStartInfo("notepad.exe", "\"" + r.Path + "\"") { UseShellExecute = true });
                }
                catch (Exception ex)
                {
                    return Loc.T("Impossible d'ouvrir le Bloc-notes : ", "Could not open Notepad: ") + ex.Message;
                }
            }
            return null;
        }

        /// <summary>Ouvre le dossier d'un rapport (sélectionné dans l'Explorateur), ou le dossier des rapports.</summary>
        public string OpenFolder(SessionReport report)
        {
            try
            {
                if (report != null)
                {
                    Process.Start(new ProcessStartInfo("explorer.exe", "/select,\"" + report.Path + "\"") { UseShellExecute = true });
                    return null;
                }
                string folder = _folders().FirstOrDefault(Directory.Exists);
                if (folder != null) Process.Start(new ProcessStartInfo("explorer.exe", "\"" + folder + "\"") { UseShellExecute = true });
                return null;
            }
            catch (Exception ex)
            {
                return Loc.T("Impossible d'ouvrir le dossier : ", "Could not open the folder: ") + ex.Message;
            }
        }

        /// <summary>
        /// Envoie des rapports à la corbeille (une erreur de sélection se rattrape). Exception de Windows :
        /// sur un disque sans corbeille (lecteur réseau, clé USB), la suppression est définitive.
        /// Retourne la liste des échecs.
        /// </summary>
        public List<string> Delete(IEnumerable<SessionReport> reports)
        {
            var failures = new List<string>();
            foreach (SessionReport r in reports)
            {
                try
                {
                    Microsoft.VisualBasic.FileIO.FileSystem.DeleteFile(r.Path, Microsoft.VisualBasic.FileIO.UIOption.OnlyErrorDialogs,
                        Microsoft.VisualBasic.FileIO.RecycleOption.SendToRecycleBin);
                }
                catch (Exception ex)
                {
                    failures.Add(Path.GetFileName(r.Path) + " : " + ex.Message);
                }
            }
            Refresh(true);
            return failures;
        }
    }
}
