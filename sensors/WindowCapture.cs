// Fenêtre du bureau (un chat, par exemple) affichée dans le casque : choix de la fenêtre, mémoire
// partagée avec la layer et XRPerfVR, et pilotage de la capture, faite par XRPerfCapture.dll
// (layer/src/window_capture.cpp) avec l'API de capture de Windows.
//
// La capture ne tourne que si quelqu'un veut l'image : la layer ou XRPerfVR (jeu VR en cours, fenêtre
// affichée), qui le signalent dans la mémoire partagée, ou l'aperçu de la fenêtre de réglages.
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace XRPerfSensors
{
    /// <summary>Une fenêtre du bureau proposée dans la liste des réglages.</summary>
    internal sealed class DesktopWindow
    {
        public IntPtr Handle;
        public string Title = "";
        public string Process = ""; // nom de l'exécutable, par exemple "chrome.exe"

        public override string ToString()
        {
            string title = Title.Length > 70 ? Title.Substring(0, 67) + "..." : Title;
            return Process.Length > 0 ? title + "  —  " + Process : title;
        }
    }

    internal sealed class WindowCapture : IDisposable
    {
        // Mémoire partagée : même disposition que WindowShareHeader (layer/src/shared_block.h).
        private const string ShareName = @"Local\XRPerfOverlay.Window";
        private const uint Magic = 0x57505258; // "XRPW"
        private const uint Version = 1;
        public const int MaxWidth = 1280, MaxHeight = 1280;
        private const int HeaderSize = 48;
        private const long ShareSize = HeaderSize + (long)MaxWidth * MaxHeight * 4;
        private const int OffsetSequence = 8, OffsetFrame = 12, OffsetWidth = 16, OffsetHeight = 20, OffsetWriterTick = 24, OffsetReaderTick = 32;

        // Lecture et écriture pour les jeux (non administrateurs, ou en conteneur d'application) : ils y
        // signalent qu'ils veulent l'image.
        private const string Sddl = "D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;AU)(A;;GRGW;;;AC)";

        // État de la capture (XrpcState dans window_capture.cpp).
        private const int StateStopped = 0, StateStarting = 1, StateCapturing = 2, StateMinimized = 3, StateClosed = 4, StateUnsupported = 5, StateFailed = 6;

        [StructLayout(LayoutKind.Sequential)]
        private struct XrpcOptions
        {
            public uint Size;
            public float Fps;
            public float CropLeft, CropTop, CropRight, CropBottom;
            public float Opacity;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct XrpcStatus
        {
            public int State;
            public int Width, Height;
            public uint Frames;
            public int Error;
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int StartFn(IntPtr window, IntPtr share, ulong shareBytes, ref XrpcOptions options);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void SetOptionsFn(ref XrpcOptions options);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void StopFn();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void StatusFn(out XrpcStatus status);

        private readonly string _dllPath;
        private bool _loadTried;
        private StartFn _start;
        private SetOptionsFn _setOptions;
        private StopFn _stop;
        private StatusFn _status;

        private IntPtr _mapping, _view;
        private IntPtr _target;          // fenêtre capturée en ce moment
        private IntPtr _chosen;          // fenêtre choisie à l'instant dans la liste (prioritaire)
        private bool _running;
        private long _wantedUntil;       // la capture s'arrête quelques secondes après la dernière demande
        private string _message = "";    // ce qui empêche la capture, pour la fenêtre de réglages

        /// <param name="folder">Dossier de XRPerfSensors.exe, où l'installation range aussi XRPerfCapture.dll.</param>
        public WindowCapture(string folder)
        {
            _dllPath = Path.Combine(folder, "XRPerfCapture.dll");
            CreateShare();
        }

        /// <summary>La bibliothèque de capture est présente (absente des installations d'avant la version 1.4).</summary>
        public bool Available { get { return File.Exists(_dllPath) && _view != IntPtr.Zero; } }

        public bool Running { get { return _running; } }

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
                _mapping = CreateFileMappingW(new IntPtr(-1), ref attributes, 0x04 /* PAGE_READWRITE */, (uint)(ShareSize >> 32), (uint)(ShareSize & 0xFFFFFFFF), ShareName);
            }
            finally
            {
                LocalFree(descriptor);
            }
            if (_mapping == IntPtr.Zero) return;
            _view = MapViewOfFile(_mapping, 0x0002 | 0x0004 /* FILE_MAP_WRITE | FILE_MAP_READ */, 0, 0, new UIntPtr((ulong)ShareSize));
            if (_view == IntPtr.Zero)
            {
                CloseHandle(_mapping);
                _mapping = IntPtr.Zero;
                return;
            }
            Marshal.WriteInt32(_view, 0, unchecked((int)Magic));
            Marshal.WriteInt32(_view, 4, (int)Version);
            Marshal.WriteInt64(_view, OffsetWriterTick, 0);
        }

        /// <summary>La layer ou XRPerfVR ont demandé l'image il y a moins de 3 secondes.</summary>
        private bool ReaderWants()
        {
            if (_view == IntPtr.Zero) return false;
            long reader = Marshal.ReadInt64(_view, OffsetReaderTick);
            long now = Environment.TickCount64;
            return reader != 0 && now >= reader && now - reader < 3000;
        }

        // --- Bibliothèque de capture ----------------------------------------------------------------

        private bool Load()
        {
            if (_loadTried) return _start != null;
            _loadTried = true;
            try
            {
                if (!File.Exists(_dllPath)) return false;
                IntPtr library = NativeLibrary.Load(_dllPath);
                _start = Marshal.GetDelegateForFunctionPointer<StartFn>(NativeLibrary.GetExport(library, "xrpcStart"));
                _setOptions = Marshal.GetDelegateForFunctionPointer<SetOptionsFn>(NativeLibrary.GetExport(library, "xrpcSetOptions"));
                _stop = Marshal.GetDelegateForFunctionPointer<StopFn>(NativeLibrary.GetExport(library, "xrpcStop"));
                _status = Marshal.GetDelegateForFunctionPointer<StatusFn>(NativeLibrary.GetExport(library, "xrpcGetStatus"));
            }
            catch (Exception)
            {
                _start = null;
            }
            return _start != null;
        }

        private static XrpcOptions OptionsOf(Settings s)
        {
            return new XrpcOptions
            {
                Size = (uint)Marshal.SizeOf<XrpcOptions>(),
                Fps = s.CaptureFps,
                CropLeft = s.CropLeft / 100f,
                CropTop = s.CropTop / 100f,
                CropRight = s.CropRight / 100f,
                CropBottom = s.CropBottom / 100f,
                Opacity = s.CaptureOpacity / 100f,
            };
        }

        /// <summary>
        /// À appeler régulièrement (chaque seconde) et après chaque changement : lance, adapte ou arrête la capture.
        /// </summary>
        /// <param name="enabled">La fenêtre est à afficher dans le casque (réglage connu ici, pour le message d'état).</param>
        /// <param name="settings">Fenêtre choisie et réglages de la capture.</param>
        /// <param name="preview">L'aperçu de la fenêtre de réglages est affiché.</param>
        public void Update(bool enabled, Settings settings, bool preview)
        {
            long now = Environment.TickCount64;
            bool hasTarget = settings.CaptureTitle.Length > 0 || settings.CaptureProcess.Length > 0;
            // La demande du jeu suffit : la fenêtre a pu être affichée en jeu par Ctrl+Alt+F (.ini pas encore relu ici).
            bool wanted = hasTarget && (preview || ReaderWants());
            if (!Available)
            {
                _message = File.Exists(_dllPath) ? "Capture impossible : mémoire partagée indisponible." : "XRPerfCapture.dll absente : réinstallez XR Perf Overlay.";
                return;
            }
            if (!hasTarget)
            {
                _message = "Aucune fenêtre choisie.";
                StopCapture();
                return;
            }
            if (wanted) _wantedUntil = now + 5000;
            if (!wanted && now >= _wantedUntil)
            {
                _message = enabled ? "En attente d'un jeu VR : la capture démarre avec lui." : "";
                StopCapture();
                return;
            }
            if (!Load())
            {
                _message = "XRPerfCapture.dll illisible : réinstallez XR Perf Overlay.";
                return;
            }
            // Fenêtre toujours là et toujours la bonne : gardée ; sinon recherchée de nouveau.
            IntPtr target;
            if (_chosen != IntPtr.Zero && IsWindow(_chosen))
                target = _chosen;
            else
                target = _target != IntPtr.Zero && IsWindow(_target) && Matches(_target, settings) ? _target : Find(settings);
            _chosen = IntPtr.Zero;
            if (target == IntPtr.Zero)
            {
                _message = "Fenêtre introuvable : « " + (settings.CaptureTitle.Length > 0 ? settings.CaptureTitle : settings.CaptureProcess) +
                           " » n'est pas ouverte.";
                StopCapture();
                return;
            }
            XrpcOptions options = OptionsOf(settings);
            if (!_running || target != _target)
            {
                if (_start(target, _view, (ulong)ShareSize, ref options) != 0)
                {
                    _message = "Capture impossible (paramètres refusés).";
                    return;
                }
                _running = true;
                _target = target;
            }
            else
            {
                _setOptions(ref options);
            }
            _message = "";
        }

        private void StopCapture()
        {
            if (!_running) return;
            _stop();
            _running = false;
            _target = IntPtr.Zero;
        }

        /// <summary>État de la capture, en une ligne, pour la fenêtre de réglages.</summary>
        public string StatusText()
        {
            if (!_running || _status == null) return _message;
            _status(out XrpcStatus status);
            switch (status.State)
            {
                case StateCapturing:
                    int width = 0, height = 0;
                    ReadSize(ref width, ref height);
                    return width > 0
                        ? string.Format("Capture en cours : fenêtre de {0} x {1}, image envoyée au casque {2} x {3}.", status.Width, status.Height, width, height)
                        : "Capture en cours.";
                case StateStarting: return "Démarrage de la capture...";
                case StateMinimized: return "Fenêtre réduite : Windows ne la dessine plus, la dernière image reste affichée.";
                case StateClosed: return "La fenêtre a été fermée.";
                case StateUnsupported: return "Capture impossible : il faut Windows 10 version 1903 ou plus récent.";
                case StateFailed: return string.Format("Cette fenêtre ne peut pas être capturée (erreur 0x{0:X8}) ; nouvel essai toutes les 3 s.", status.Error);
                default: return _message;
            }
        }

        private void ReadSize(ref int width, ref int height)
        {
            if (_view == IntPtr.Zero) return;
            width = Marshal.ReadInt32(_view, OffsetWidth);
            height = Marshal.ReadInt32(_view, OffsetHeight);
        }

        private int _previewFrame = -1;

        /// <summary>
        /// Dernière image capturée (BGRA prémultiplié, comme une image Windows PArgb), ou null si elle n'a pas
        /// changé depuis l'appel précédent ou si la capture ne tourne pas.
        /// </summary>
        public Bitmap TakePreview()
        {
            if (!_running || _view == IntPtr.Zero) return null;
            for (int attempt = 0; attempt < 2; attempt++)
            {
                int before = Marshal.ReadInt32(_view, OffsetSequence);
                if ((before & 1) != 0) return null;
                int frame = Marshal.ReadInt32(_view, OffsetFrame);
                if (frame == _previewFrame) return null;
                int width = Marshal.ReadInt32(_view, OffsetWidth), height = Marshal.ReadInt32(_view, OffsetHeight);
                if (width <= 0 || height <= 0 || width > MaxWidth || height > MaxHeight) return null;
                var bitmap = new Bitmap(width, height, PixelFormat.Format32bppPArgb);
                BitmapData data = bitmap.LockBits(new Rectangle(0, 0, width, height), ImageLockMode.WriteOnly, PixelFormat.Format32bppPArgb);
                try
                {
                    var row = new byte[width * 4];
                    for (int y = 0; y < height; y++)
                    {
                        Marshal.Copy(IntPtr.Add(_view, HeaderSize + y * width * 4), row, 0, row.Length);
                        Marshal.Copy(row, 0, IntPtr.Add(data.Scan0, y * data.Stride), row.Length);
                    }
                }
                finally
                {
                    bitmap.UnlockBits(data);
                }
                if (Marshal.ReadInt32(_view, OffsetSequence) != before)
                {
                    bitmap.Dispose();
                    continue;
                }
                _previewFrame = frame;
                return bitmap;
            }
            return null;
        }

        /// <summary>
        /// Nouvelle fenêtre choisie dans la liste : c'est elle qui est capturée (même si une autre fenêtre du
        /// même programme l'était déjà), au prochain Update.
        /// </summary>
        public void Retarget(IntPtr window)
        {
            _chosen = window;
            _previewFrame = -1;
        }

        /// <summary>Oublie l'image déjà montrée : la prochaine prévisualisation la redonne.</summary>
        public void ResetPreview() { _previewFrame = -1; }

        public void Dispose()
        {
            if (_running && _stop != null) _stop();
            _running = false;
            if (_view != IntPtr.Zero)
            {
                Marshal.WriteInt64(_view, OffsetWriterTick, 0);
                UnmapViewOfFile(_view);
                _view = IntPtr.Zero;
            }
            if (_mapping != IntPtr.Zero)
            {
                CloseHandle(_mapping);
                _mapping = IntPtr.Zero;
            }
        }

        // --- Fenêtres du bureau ---------------------------------------------------------------------

        private delegate bool EnumWindowsProc(IntPtr window, IntPtr parameter);

        [DllImport("user32.dll")]
        private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr parameter);

        [DllImport("user32.dll")]
        private static extern bool IsWindow(IntPtr window);

        [DllImport("user32.dll")]
        private static extern bool IsWindowVisible(IntPtr window);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern int GetWindowTextW(IntPtr window, StringBuilder text, int maxCount);

        [DllImport("user32.dll")]
        private static extern int GetWindowTextLengthW(IntPtr window);

        [DllImport("user32.dll")]
        private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

        [DllImport("user32.dll")]
        private static extern IntPtr GetWindow(IntPtr window, uint command);

        [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
        private static extern IntPtr GetWindowLongPtr(IntPtr window, int index);

        [DllImport("dwmapi.dll")]
        private static extern int DwmGetWindowAttribute(IntPtr window, int attribute, out int value, int size);

        private const int GWL_EXSTYLE = -20;
        private const long WS_EX_TOOLWINDOW = 0x80, WS_EX_APPWINDOW = 0x40000;
        private const uint GW_OWNER = 4;
        private const int DWMWA_CLOAKED = 14;

        private static string TitleOf(IntPtr window)
        {
            int length = GetWindowTextLengthW(window);
            if (length <= 0) return "";
            var text = new StringBuilder(length + 1);
            GetWindowTextW(window, text, text.Capacity);
            return text.ToString();
        }

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr OpenProcess(uint access, bool inherit, uint processId);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool QueryFullProcessImageNameW(IntPtr process, uint flags, StringBuilder name, ref uint size);

        // Nom de l'exécutable d'une fenêtre (lecture directe, sans parcourir tous les processus).
        private static string ProcessOf(IntPtr window)
        {
            GetWindowThreadProcessId(window, out uint pid);
            IntPtr process = OpenProcess(0x1000 /* PROCESS_QUERY_LIMITED_INFORMATION */, false, pid);
            if (process == IntPtr.Zero) return "";
            try
            {
                var name = new StringBuilder(1024);
                uint size = (uint)name.Capacity;
                return QueryFullProcessImageNameW(process, 0, name, ref size) ? Path.GetFileName(name.ToString(0, (int)size)) : "";
            }
            finally
            {
                CloseHandle(process);
            }
        }

        // Fenêtre principale visible d'une application (celles de la barre des tâches, à peu près).
        private static bool IsCandidate(IntPtr window)
        {
            if (!IsWindowVisible(window) || GetWindowTextLengthW(window) <= 0) return false;
            long style = GetWindowLongPtr(window, GWL_EXSTYLE).ToInt64();
            if ((style & WS_EX_TOOLWINDOW) != 0) return false;
            if (GetWindow(window, GW_OWNER) != IntPtr.Zero && (style & WS_EX_APPWINDOW) == 0) return false;
            // Fenêtres « masquées » par Windows (applications du Store suspendues, autres bureaux virtuels...).
            if (DwmGetWindowAttribute(window, DWMWA_CLOAKED, out int cloaked, sizeof(int)) == 0 && cloaked != 0) return false;
            return true;
        }

        /// <summary>Fenêtres ouvertes que l'on peut choisir (sauf celles de XRPerfSensors).</summary>
        public static List<DesktopWindow> ListWindows()
        {
            var list = new List<DesktopWindow>();
            uint own = (uint)Environment.ProcessId;
            EnumWindows((window, parameter) =>
            {
                if (!IsCandidate(window)) return true;
                GetWindowThreadProcessId(window, out uint pid);
                if (pid == own) return true;
                list.Add(new DesktopWindow { Handle = window, Title = TitleOf(window), Process = ProcessOf(window) });
                return true;
            }, IntPtr.Zero);
            return list;
        }

        private static bool Matches(IntPtr window, Settings s)
        {
            return s.CaptureProcess.Length == 0 || string.Equals(ProcessOf(window), s.CaptureProcess, StringComparison.OrdinalIgnoreCase);
        }

        /// <summary>
        /// Retrouve la fenêtre choisie : même programme, et titre identique, ou qui contient l'ancien (ou
        /// l'inverse : le titre d'un navigateur ou d'un chat change avec la page ou le salon). À défaut, la
        /// seule fenêtre de ce programme.
        /// </summary>
        public static IntPtr Find(Settings s)
        {
            List<DesktopWindow> windows = ListWindows();
            var sameProcess = windows.FindAll(w => s.CaptureProcess.Length == 0 || string.Equals(w.Process, s.CaptureProcess, StringComparison.OrdinalIgnoreCase));
            if (s.CaptureTitle.Length > 0)
            {
                DesktopWindow exact = sameProcess.Find(w => string.Equals(w.Title, s.CaptureTitle, StringComparison.OrdinalIgnoreCase));
                if (exact != null) return exact.Handle;
                DesktopWindow close = sameProcess.Find(w =>
                    w.Title.IndexOf(s.CaptureTitle, StringComparison.OrdinalIgnoreCase) >= 0 || s.CaptureTitle.IndexOf(w.Title, StringComparison.OrdinalIgnoreCase) >= 0);
                if (close != null) return close.Handle;
            }
            return s.CaptureProcess.Length > 0 && sameProcess.Count == 1 ? sameProcess[0].Handle : IntPtr.Zero;
        }
    }
}
