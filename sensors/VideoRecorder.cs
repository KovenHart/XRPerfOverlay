// Enregistrement vidéo du casque : bloc de mémoire partagée avec la layer et XRPerfVR (qui y décrivent
// l'image d'un œil, copiée dans des textures partagées), pilotage de l'enregistreur XRPerfRecord.dll
// (layer/src/video_record.cpp) et raccourci clavier global (Ctrl+Alt+V par défaut).
//
// Rien ne tourne tant que personne n'enregistre : le bloc dit seulement « pas d'enregistrement ».
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace XRPerfSensors
{
    internal sealed class VideoRecorder : IDisposable
    {
        // Mémoire partagée : même disposition que VideoShareHeader (layer/src/shared_block.h).
        private const string ShareName = @"Local\XRPerfOverlay.Video";
        private const uint Magic = 0x56505258; // "XRPV"
        private const uint Version = 4;
        // En-tête (4096 octets), puis l'image du panneau pour la vidéo : 640 x 320 pixels RGBA (kVideoShareBytes).
        private const int ShareSize = 4096 + 640 * 320 * 4;
        // Lecture et écriture pour les jeux (non administrateurs, en conteneur d'application ou à faible
        // niveau d'intégrité : l'étiquette « LW » leur permet d'y écrire).
        private const string Sddl = "D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;AU)(A;;GRGW;;;AC)S:(ML;;NW;;;LW)";

        // États et fins (video_record.cpp).
        public const int StateStopped = 0, StateWaiting = 1, StateRecording = 2, StateFinishing = 3;

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct XrprOptions
        {
            public uint Size;
            public uint Codec;
            public uint Height;
            public uint Fps;
            public uint BitrateKbps;
            public uint Eye;
            public float Aspect;
            public float Field;
            public uint GameAudio;
            public uint MicAudio;
            public uint QualityVsSpeed; // 1 à 100 (plus haut : meilleure image, encodage plus lent) ; 0 : réglage de l'encodeur
            public uint Overlay;        // panneau dans la vidéo : 0 non, 1 à sa place dans le casque, 2 dans un coin
            public uint OverlayCorner;  // 0 haut gauche, 1 haut droite, 2 bas gauche, 3 bas droite
            public float OverlaySize;   // largeur du panneau dans un coin, part de la largeur de la vidéo
            public float StabilizeSeconds; // stabilisation : temps de retard de la caméra (0 : aucune)
            public uint Level;          // 1 : horizon droit
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)] public string Folder;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string RenderDevice;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string CaptureDevice;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        public struct Status
        {
            public int State;
            public int Reason;
            public double Seconds;
            public ulong Bytes;
            public uint Frames;
            public uint Repeated;
            public uint Dropped;
            public uint Audio;
            public int HResult;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)] public string File;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string Game;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string Encoder;
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int ProbeFn();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int StartFn(IntPtr share, ulong shareBytes, ref XrprOptions options);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void StopFn();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void StatusFn(out Status status);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int DevicesFn(int capture, IntPtr buffer, int chars);

        private readonly string _dllPath;
        private bool _loadTried;
        private ProbeFn _probe;
        private StartFn _start;
        private StopFn _stop;
        private StatusFn _status;
        private DevicesFn _devices;
        private IntPtr _mapping, _view;
        private volatile int _codecs = -1;   // encodeurs matériels présents (bits : H.264, HEVC, AV1) ; -1 : pas encore connu
        private Task _stopping;
        private string _message = "";        // dernier refus de démarrage

        /// <param name="folder">Dossier de XRPerfSensors.exe, où l'installation range aussi XRPerfRecord.dll.</param>
        public VideoRecorder(string folder)
        {
            _dllPath = Path.Combine(folder, "XRPerfRecord.dll");
            CreateShare();
            // Encodeurs présents : la recherche prend un instant, faite à part.
            if (Available) Task.Run(() =>
            {
                try
                {
                    _codecs = Load() ? _probe() : 0;
                }
                catch (Exception)
                {
                    _codecs = 0;
                }
                CodecsKnown?.Invoke();
            });
            else _codecs = 0;
        }

        /// <summary>Appelé (sur un autre fil) quand les encodeurs présents sont connus.</summary>
        public event Action CodecsKnown;

        /// <summary>La bibliothèque est présente (absente des installations d'avant la version 1.9).</summary>
        public bool Available { get { return File.Exists(_dllPath) && _view != IntPtr.Zero; } }

        /// <summary>Encodeurs matériels : 1 = H.264, 2 = H.265, 4 = AV1 ; -1 tant que la recherche n'est pas finie.</summary>
        public int Codecs { get { return _codecs; } }

        /// <summary>Débit utilisé (Mbit/s) : celui de la qualité choisie, ou celui réglé à la main.</summary>
        public static int Bitrate(Settings s)
        {
            if (s.VideoQuality == "custom") return s.VideoBitrate;
            float aspect = s.VideoAspect == "4:3" ? 4f / 3f : s.VideoAspect == "1:1" ? 1f : 16f / 9f;
            return PresetBitrate(s.VideoQuality, s.VideoCodec, s.VideoHeight, aspect, s.VideoFps);
        }

        /// <summary>
        /// Débit d'une qualité (Mbit/s). Référence : H.264, 1920 x 1080, 60 images/s ; H.265 et AV1 compressent
        /// mieux (70 % et 60 % du débit pour une image comparable) ; le débit suit le nombre de pixels et la
        /// cadence un peu moins que proportionnellement (exposants 0,8 et 0,75). Même calcul dans ui/app.js
        /// (presetBitrate), qui affiche le résultat.
        /// </summary>
        public static int PresetBitrate(string quality, string codec, int height, float aspect, int fps)
        {
            double reference = quality == "light" ? 10 : quality == "balanced" ? 16 : quality == "max" ? 40 : 24;
            double codecFactor = codec == "hevc" ? 0.7 : codec == "av1" ? 0.6 : 1.0;
            double pixels = (double)height * Math.Round(height * aspect, MidpointRounding.AwayFromZero) / (1920.0 * 1080.0);
            double mbps = reference * codecFactor * Math.Pow(pixels, 0.8) * Math.Pow(fps / 60.0, 0.75);
            return (int)Math.Max(Settings.MinVideoBitrate, Math.Min(Settings.MaxVideoBitrate, Math.Round(mbps, MidpointRounding.AwayFromZero)));
        }

        // Compromis vitesse / qualité de l'encodeur (CODECAPI_AVEncCommonQualityVsSpeed) ; à la main : celui de l'encodeur.
        private static int QualityVsSpeedOf(string quality)
        {
            return quality == "light" ? 33 : quality == "balanced" ? 50 : quality == "high" ? 70 : quality == "max" ? 100 : 0;
        }

        public static int CodecBit(string codec) { return codec == "hevc" ? 2 : codec == "av1" ? 4 : 1; }

        public string Message { get { return _message; } }

        // --- Mémoire partagée ---------------------------------------------------------------------

        [StructLayout(LayoutKind.Sequential)]
        private struct SECURITY_ATTRIBUTES
        {
            public int nLength;
            public IntPtr lpSecurityDescriptor;
            public int bInheritHandle;
        }

        [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern bool ConvertStringSecurityDescriptorToSecurityDescriptorW(
            string stringSecurityDescriptor, uint revision, out IntPtr securityDescriptor, IntPtr securityDescriptorSize);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern IntPtr CreateFileMappingW(
            IntPtr file, ref SECURITY_ATTRIBUTES attributes, uint protect, uint maximumSizeHigh, uint maximumSizeLow, string name);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr MapViewOfFile(IntPtr mapping, uint desiredAccess, uint offsetHigh, uint offsetLow, UIntPtr bytes);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool UnmapViewOfFile(IntPtr address);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CloseHandle(IntPtr handle);

        [DllImport("kernel32.dll")]
        private static extern IntPtr LocalFree(IntPtr memory);

        private void CreateShare()
        {
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(Sddl, 1, out IntPtr descriptor, IntPtr.Zero)) return;
            try
            {
                var attributes = new SECURITY_ATTRIBUTES { nLength = Marshal.SizeOf<SECURITY_ATTRIBUTES>(), lpSecurityDescriptor = descriptor };
                _mapping = CreateFileMappingW(new IntPtr(-1), ref attributes, 0x04 /* PAGE_READWRITE */, 0, ShareSize, ShareName);
            }
            finally
            {
                LocalFree(descriptor);
            }
            if (_mapping == IntPtr.Zero) return;
            _view = MapViewOfFile(_mapping, 0x0002 | 0x0004 /* FILE_MAP_WRITE | FILE_MAP_READ */, 0, 0, new UIntPtr(ShareSize));
            if (_view == IntPtr.Zero)
            {
                CloseHandle(_mapping);
                _mapping = IntPtr.Zero;
                return;
            }
            // Mémoire neuve (ou reprise d'une instance précédente) : pas d'enregistrement en cours.
            Marshal.WriteInt32(_view, 0, unchecked((int)Magic));
            Marshal.WriteInt32(_view, 4, (int)Version);
            Marshal.WriteInt32(_view, 8, 0);         // wanted
            Marshal.WriteInt64(_view, 32, 0);        // recordStartTickMs
        }

        // --- Bibliothèque -----------------------------------------------------------------------------

        private bool Load()
        {
            lock (this)
            {
                if (_loadTried) return _start != null;
                _loadTried = true;
                try
                {
                    if (!File.Exists(_dllPath)) return false;
                    IntPtr library = NativeLibrary.Load(_dllPath);
                    _probe = Marshal.GetDelegateForFunctionPointer<ProbeFn>(NativeLibrary.GetExport(library, "XrprProbe"));
                    _start = Marshal.GetDelegateForFunctionPointer<StartFn>(NativeLibrary.GetExport(library, "XrprStart"));
                    _stop = Marshal.GetDelegateForFunctionPointer<StopFn>(NativeLibrary.GetExport(library, "XrprStop"));
                    _status = Marshal.GetDelegateForFunctionPointer<StatusFn>(NativeLibrary.GetExport(library, "XrprGetStatus"));
                    _devices = Marshal.GetDelegateForFunctionPointer<DevicesFn>(NativeLibrary.GetExport(library, "XrprListDevices"));
                }
                catch (Exception)
                {
                    _start = null;
                }
                return _start != null;
            }
        }

        public Status Current()
        {
            var status = new Status { File = "", Game = "", Encoder = "" };
            if (_status != null)
            {
                try
                {
                    _status(out status);
                }
                catch (Exception)
                {
                }
            }
            if (_stopping != null && !_stopping.IsCompleted && status.State == StateStopped) status.State = StateFinishing;
            return status;
        }

        public bool Busy { get { int s = Current().State; return s != StateStopped; } }

        /// <summary>Démarre un enregistrement. Retourne null, ou la raison du refus (texte pour l'interface).</summary>
        public string Start(Settings s)
        {
            _message = "";
            if (!Available)
                _message = File.Exists(_dllPath) ? Loc.T("Enregistrement impossible : mémoire partagée indisponible.", "Recording impossible: shared memory unavailable.")
                                                 : Loc.T("XRPerfRecord.dll absente : réinstallez XR Perf Overlay.", "XRPerfRecord.dll is missing: reinstall XR Perf Overlay.");
            else if (!Load())
                _message = Loc.T("XRPerfRecord.dll illisible : réinstallez XR Perf Overlay.", "XRPerfRecord.dll is unreadable: reinstall XR Perf Overlay.");
            else if (_stopping != null && !_stopping.IsCompleted)
                _message = Loc.T("Le fichier précédent est en cours de finalisation, un instant.", "The previous file is being finalized, one moment.");
            else if (_codecs >= 0 && (_codecs & CodecBit(s.VideoCodec)) == 0)
                _message = string.Format(Loc.T("Pas d'encodeur matériel {0} sur ce PC : choisissez un autre format.", "No {0} hardware encoder on this PC: choose another format."), CodecName(s.VideoCodec));
            if (_message.Length > 0) return _message;
            float aspect = s.VideoAspect == "4:3" ? 4f / 3f : s.VideoAspect == "1:1" ? 1f : 16f / 9f;
            var options = new XrprOptions
            {
                Size = (uint)Marshal.SizeOf<XrprOptions>(),
                Codec = (uint)Math.Max(0, Array.IndexOf(Settings.VideoCodecs, s.VideoCodec)),
                Height = (uint)s.VideoHeight,
                Fps = (uint)s.VideoFps,
                BitrateKbps = (uint)(Bitrate(s) * 1000),
                QualityVsSpeed = (uint)QualityVsSpeedOf(s.VideoQuality),
                Overlay = (uint)Math.Max(0, Array.IndexOf(Settings.VideoOverlays, s.VideoOverlay)),
                OverlayCorner = (uint)Math.Max(0, Array.IndexOf(Settings.VideoCorners, s.VideoOverlayCorner)),
                OverlaySize = s.VideoOverlaySize / 100f,
                StabilizeSeconds = s.VideoStabilize == "strong" ? 0.35f : s.VideoStabilize == "light" ? 0.12f : 0f,
                Level = s.VideoLevel ? 1u : 0u,
                Eye = (uint)s.VideoEye,
                Aspect = aspect,
                Field = s.VideoField / 100f,
                GameAudio = s.VideoGameAudio ? 1u : 0u,
                MicAudio = s.VideoMic ? 1u : 0u,
                Folder = s.VideoFolderOrDefault,
                RenderDevice = s.VideoRenderDevice ?? "",
                CaptureDevice = s.VideoCaptureDevice ?? "",
            };
            int result;
            try
            {
                result = _start(_view, ShareSize, ref options);
            }
            catch (Exception)
            {
                result = -1;
            }
            if (result == 0) return null;
            _message = result == 1 ? Loc.T("Un enregistrement est déjà en cours.", "A recording is already running.")
                                   : Loc.T("Enregistrement impossible (réglages refusés).", "Recording impossible (settings rejected).");
            return _message;
        }

        /// <summary>Arrête l'enregistrement ; la fin du fichier (quelques instants) se fait à part.</summary>
        public Task StopAsync()
        {
            if (_stop == null) return Task.CompletedTask;
            if (_stopping != null && !_stopping.IsCompleted) return _stopping;
            StopFn stop = _stop;
            _stopping = Task.Run(() =>
            {
                try
                {
                    stop();
                }
                catch (Exception)
                {
                }
            });
            return _stopping;
        }

        public static string CodecName(string codec)
        {
            return codec == "hevc" ? "H.265 (HEVC)" : codec == "av1" ? "AV1" : "H.264";
        }

        /// <summary>Raison de la dernière fin, en une phrase (vide pour un arrêt demandé).</summary>
        public static string ReasonText(int reason, int hresult)
        {
            string code = hresult != 0 ? string.Format(" (0x{0:X8})", hresult) : "";
            switch (reason)
            {
                case 0:
                case 31: return "";
                case 3: return Loc.T("Media Foundation est indisponible sur ce Windows (édition N sans le pack multimédia ?).", "Media Foundation is unavailable on this Windows (N edition without the media pack?).") + code;
                case 10: return Loc.T("Carte graphique du jeu inaccessible pour l'enregistrement.", "The game's graphics card cannot be used for recording.") + code;
                case 11: return Loc.T("La carte graphique ne permet pas la conversion de l'image (NV12).", "The graphics card does not allow converting the image (NV12).") + code;
                case 12: return Loc.T("Image du jeu illisible : textures partagées introuvables.", "Game image unreadable: shared textures not found.") + code;
                case 20: return Loc.T("Encodeur refusé pour ce format : essayez H.264, une résolution ou une cadence plus basse.", "Encoder refused this format: try H.264, a lower resolution or frame rate.") + code;
                case 21: return Loc.T("Impossible de créer le fichier vidéo dans le dossier choisi.", "Could not create the video file in the chosen folder.") + code;
                case 22: return Loc.T("Disque plein : le fichier a été terminé.", "Disk full: the file was finalized.") + code;
                case 23: return Loc.T("L'encodeur ou le fichier MP4 a refusé une image : essayez un autre format (H.265 ou H.264). Détails dans XRPerfRecord.log.",
                                      "The encoder or the MP4 file refused a frame: try another format (H.265 or H.264). Details in XRPerfRecord.log.") + code;
                case 24: return Loc.T("Le son a été refusé par l'encodeur AAC : essayez sans le micro ou sans le son du jeu. Détails dans XRPerfRecord.log.",
                                      "The sound was refused by the AAC encoder: try without the microphone or the game sound. Details in XRPerfRecord.log.") + code;
                case 30: return Loc.T("Le jeu s'est arrêté : le fichier a été terminé.", "The game stopped: the file was finalized.");
                default: return Loc.T("Enregistrement arrêté sur une erreur.", "Recording stopped on an error.") + code;
            }
        }

        /// <summary>Sorties (capture = false) ou entrées audio actives : identifiant et nom.</summary>
        public List<KeyValuePair<string, string>> Devices(bool capture)
        {
            var list = new List<KeyValuePair<string, string>>();
            if (!Available || !Load() || _devices == null) return list;
            try
            {
                int needed = _devices(capture ? 1 : 0, IntPtr.Zero, 0);
                if (needed <= 1 || needed > 1 << 20) return list;
                IntPtr buffer = Marshal.AllocHGlobal(needed * 2);
                try
                {
                    int written = _devices(capture ? 1 : 0, buffer, needed);
                    string text = written == needed ? Marshal.PtrToStringUni(buffer) ?? "" : "";
                    foreach (string line in text.Split('\n'))
                    {
                        int tab = line.IndexOf('\t');
                        if (tab > 0) list.Add(new KeyValuePair<string, string>(line.Substring(0, tab), line.Substring(tab + 1)));
                    }
                }
                finally
                {
                    Marshal.FreeHGlobal(buffer);
                }
            }
            catch (Exception)
            {
            }
            return list;
        }

        public void Dispose()
        {
            // Programme qui se ferme pendant un enregistrement : le fichier est terminé avant la fin.
            bool stopped = true;
            try
            {
                if (_stop != null) stopped = StopAsync().Wait(15000);
            }
            catch (Exception)
            {
            }
            // Fin du fichier encore en cours : la mémoire reste en place (l'enregistreur y écrit encore),
            // Windows la libère à la fin du programme.
            if (!stopped) return;
            if (_view != IntPtr.Zero)
            {
                Marshal.WriteInt32(_view, 8, 0);
                UnmapViewOfFile(_view);
            }
            if (_mapping != IntPtr.Zero) CloseHandle(_mapping);
            _view = _mapping = IntPtr.Zero;
        }
    }

    /// <summary>Raccourci clavier global (Ctrl+Alt+touche), reçu même quand un jeu a le clavier.</summary>
    internal sealed class GlobalHotkey : NativeWindow, IDisposable
    {
        private const int WM_HOTKEY = 0x0312;
        private const uint MOD_ALT = 0x0001, MOD_CONTROL = 0x0002, MOD_NOREPEAT = 0x4000;
        private const int Id = 0x5650; // "VP"

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool RegisterHotKey(IntPtr window, int id, uint modifiers, uint key);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool UnregisterHotKey(IntPtr window, int id);

        private int _key;
        private bool _registered;

        public event Action Pressed;

        public GlobalHotkey()
        {
            CreateHandle(new CreateParams());
        }

        /// <summary>Touche voulue (0 : aucune). Retourne false si Windows la refuse (déjà prise par un autre programme).</summary>
        public bool Set(int key)
        {
            if (key == _key && _registered) return true;
            if (_registered) UnregisterHotKey(Handle, Id);
            _registered = false;
            _key = key;
            if (key == 0) return true;
            _registered = RegisterHotKey(Handle, Id, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, (uint)key);
            return _registered;
        }

        public bool Registered { get { return _registered; } }

        protected override void WndProc(ref Message m)
        {
            if (m.Msg == WM_HOTKEY && m.WParam.ToInt32() == Id)
            {
                Pressed?.Invoke();
                return;
            }
            base.WndProc(ref m);
        }

        public void Dispose()
        {
            if (_registered) UnregisterHotKey(Handle, Id);
            _registered = false;
            DestroyHandle();
        }
    }
}
