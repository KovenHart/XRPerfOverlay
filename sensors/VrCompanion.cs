// XRPerfVR.exe : le programme qui affiche le panneau, la montre et écrit les rapports dans les jeux
// SteamVR (OpenVR). XRPerfSensors le lance quand l'option est active, le relance s'il s'arrête, et
// l'arrête proprement (rapport en cours terminé) quand l'option est désactivée ou à la fermeture.
using System;
using System.Diagnostics;
using System.IO;
using System.Threading;

namespace XRPerfSensors
{
    internal sealed class VrCompanion : IDisposable
    {
        private const string StopEventName = @"Local\XRPerfOverlay.VR.Stop";
        private const int MaxStartsPerWindow = 5;
        private static readonly TimeSpan StartWindow = TimeSpan.FromMinutes(10);

        private readonly string _exe;
        private Process _process;
        private DateTime _windowStart = DateTime.MinValue;
        private int _starts;
        private bool _gaveUp;
        // XRPerfVR s'est arrêté parce que SteamVR se fermait (code 2) : relancé aussitôt, sans compter
        // comme un arrêt inattendu, avec --after-quit pour attendre que SteamVR soit vraiment parti.
        private bool _afterQuit;

        /// <param name="folder">Dossier de XRPerfSensors.exe, où l'installation range aussi XRPerfVR.exe.</param>
        public VrCompanion(string folder)
        {
            _exe = Path.Combine(folder, "XRPerfVR.exe");
        }

        /// <summary>Le programme est présent (absent des installations d'avant la version 1.3).</summary>
        public bool Available { get { return File.Exists(_exe); } }

        /// <summary>Trop d'arrêts inattendus en peu de temps : plus de relance jusqu'au prochain changement d'option.</summary>
        public bool GaveUp { get { return _gaveUp; } }

        /// <summary>À appeler régulièrement : lance ou arrête XRPerfVR selon l'option.</summary>
        public void Update(bool wanted)
        {
            if (!wanted)
            {
                Stop();
                _gaveUp = false;
                _starts = 0;
                return;
            }
            if (!Available || _gaveUp || Running()) return;
            // Une instance lancée par un XRPerfSensors précédent se termine d'elle-même (elle surveille
            // son lanceur) : on la laisse finir avant de lancer la nôtre.
            if (OtherInstanceRunning()) return;
            bool afterQuit = _afterQuit;
            _afterQuit = false;
            if (!afterQuit)
            {
                DateTime now = DateTime.UtcNow;
                if (now - _windowStart > StartWindow)
                {
                    _windowStart = now;
                    _starts = 0;
                }
                if (_starts >= MaxStartsPerWindow)
                {
                    _gaveUp = true;
                    return;
                }
                _starts++;
            }
            try
            {
                string arguments = "--parent " + Environment.ProcessId + (afterQuit ? " --after-quit" : "");
                var start = new ProcessStartInfo(_exe, arguments)
                {
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    WorkingDirectory = Path.GetDirectoryName(_exe) ?? "",
                };
                _process = Process.Start(start);
            }
            catch (Exception)
            {
                _process = null; // nouvel essai au prochain appel, dans la limite des relances
            }
        }

        private bool Running()
        {
            if (_process == null) return false;
            try
            {
                if (!_process.HasExited) return true;
                _afterQuit = _process.ExitCode == 2;
            }
            catch (Exception)
            {
                // Processus inaccessible : considéré comme arrêté.
            }
            _process.Dispose();
            _process = null;
            return false;
        }

        // Autre XRPerfVR dans la même session Windows (celui d'un autre utilisateur ne compte pas).
        private static bool OtherInstanceRunning()
        {
            int session = Process.GetCurrentProcess().SessionId;
            bool found = false;
            foreach (Process process in Process.GetProcessesByName("XRPerfVR"))
            {
                try
                {
                    found = found || process.SessionId == session;
                }
                catch (Exception)
                {
                    // Processus terminé entre-temps.
                }
                process.Dispose();
            }
            return found;
        }

        /// <summary>Arrêt propre : XRPerfVR termine le rapport en cours, retire ses calques et quitte SteamVR.</summary>
        public void Stop()
        {
            if (!Running()) return;
            try
            {
                if (EventWaitHandle.TryOpenExisting(StopEventName, out EventWaitHandle stop))
                {
                    using (stop) stop.Set();
                }
                if (!_process.WaitForExit(3000)) _process.Kill();
            }
            catch (Exception)
            {
                // Déjà arrêté entre-temps.
            }
            _process.Dispose();
            _process = null;
        }

        public void Dispose()
        {
            Stop();
        }
    }
}
