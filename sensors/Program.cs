// XRPerfSensors : lit les capteurs du PC (LibreHardwareMonitor) et les publie deux fois par
// seconde dans une mémoire partagée que la layer OpenXR affiche dans le casque.
// Sa fenêtre principale (MainWindow.cs, interface dans le dossier ui) règle l'overlay et montre les
// rapports ; il peut aussi afficher le panneau dans une fenêtre du bureau (voir PanelWindow.cs).
//
// Tourne en dehors du jeu : s'il plante ou n'est pas lancé, le jeu n'est pas affecté et
// l'overlay continue d'afficher les FPS.

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;
using LibreHardwareMonitor.Hardware;

namespace XRPerfSensors
{
    /// <summary>Valeurs publiées. NaN = indisponible.</summary>
    internal sealed class Readings
    {
        public float CpuLoad = float.NaN, CpuTemp = float.NaN, CpuMHz = float.NaN;
        public float GpuLoad = float.NaN, GpuTemp = float.NaN, GpuMHz = float.NaN;
        public float VramUsedMB = float.NaN, VramTotalMB = float.NaN;
        public string CpuName = "?", GpuName = "?";
    }

    /// <summary>
    /// Mémoire partagée "Local\XRPerfOverlay.Sensors".
    /// La disposition doit rester identique à layer/src/shared_block.h.
    /// </summary>
    internal sealed class SharedBlock : IDisposable
    {
        private const string Name = @"Local\XRPerfOverlay.Sensors";
        private const int Size = 56;
        private const uint Magic = 0x4F505258; // "XRPO"
        private const uint Version = 1;

        private const uint PAGE_READWRITE = 0x04;
        private const uint FILE_MAP_WRITE = 0x0002;
        private const uint SDDL_REVISION_1 = 1;
        private static readonly IntPtr InvalidHandle = new IntPtr(-1);

        // Ce programme tourne en administrateur alors que le jeu ne l'est pas : sans cette
        // autorisation explicite, le jeu n'aurait pas le droit d'ouvrir la mémoire partagée.
        // Lecture seule pour les utilisateurs authentifiés (AU) et les applications en bac à sable (AC).
        private const string Sddl = "D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GR;;;AU)(A;;GR;;;AC)";

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

        private IntPtr _mapping;
        private IntPtr _view;
        private int _sequence;

        public SharedBlock()
        {
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(Sddl, SDDL_REVISION_1, out IntPtr descriptor, IntPtr.Zero))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            try
            {
                var attributes = new SECURITY_ATTRIBUTES
                {
                    nLength = Marshal.SizeOf<SECURITY_ATTRIBUTES>(),
                    lpSecurityDescriptor = descriptor,
                    bInheritHandle = 0,
                };
                // Si un jeu en cours garde encore l'ancienne mémoire ouverte, on la réutilise simplement.
                _mapping = CreateFileMappingW(InvalidHandle, ref attributes, PAGE_READWRITE, 0, Size, Name);
                if (_mapping == IntPtr.Zero)
                    throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            finally
            {
                LocalFree(descriptor);
            }

            _view = MapViewOfFile(_mapping, FILE_MAP_WRITE, 0, 0, new UIntPtr(Size));
            if (_view == IntPtr.Zero)
            {
                int error = Marshal.GetLastWin32Error();
                CloseHandle(_mapping);
                _mapping = IntPtr.Zero;
                throw new Win32Exception(error);
            }

            _sequence = Marshal.ReadInt32(_view, 8) & ~1; // reprend une séquence paire
            Marshal.WriteInt32(_view, 0, unchecked((int)Magic));
            Marshal.WriteInt32(_view, 4, unchecked((int)Version));
        }

        public void Write(Readings r)
        {
            if (_view == IntPtr.Zero) return;

            // Séquence impaire pendant l'écriture : le lecteur sait qu'il doit relire.
            Marshal.WriteInt32(_view, 8, ++_sequence);
            Thread.MemoryBarrier();

            Marshal.WriteInt64(_view, 16, Environment.TickCount64);
            WriteFloat(24, r.CpuLoad);
            WriteFloat(28, r.CpuTemp);
            WriteFloat(32, r.CpuMHz);
            WriteFloat(36, r.GpuLoad);
            WriteFloat(40, r.GpuTemp);
            WriteFloat(44, r.GpuMHz);
            WriteFloat(48, r.VramUsedMB);
            WriteFloat(52, r.VramTotalMB);

            Thread.MemoryBarrier();
            Marshal.WriteInt32(_view, 8, ++_sequence);
        }

        private void WriteFloat(int offset, float value)
        {
            Marshal.WriteInt32(_view, offset, BitConverter.SingleToInt32Bits(value));
        }

        public void Dispose()
        {
            if (_view != IntPtr.Zero)
            {
                // Horodatage à zéro : la layer voit immédiatement que les données ne sont plus valables.
                Marshal.WriteInt32(_view, 8, ++_sequence);
                Marshal.WriteInt64(_view, 16, 0);
                Marshal.WriteInt32(_view, 8, ++_sequence);
                UnmapViewOfFile(_view);
                _view = IntPtr.Zero;
            }
            if (_mapping != IntPtr.Zero)
            {
                CloseHandle(_mapping);
                _mapping = IntPtr.Zero;
            }
        }
    }

    /// <summary>Choisit le processeur et la carte graphique à suivre, puis lit leurs capteurs.</summary>
    internal sealed class SensorSource : IDisposable
    {
        private readonly Computer _computer;
        private readonly IHardware _cpu;
        private readonly IHardware _gpu;

        public SensorSource()
        {
            _computer = new Computer { IsCpuEnabled = true, IsGpuEnabled = true };
            _computer.Open();

            float bestScore = -1f;
            foreach (IHardware hardware in _computer.Hardware)
            {
                Update(hardware);
                switch (hardware.HardwareType)
                {
                    case HardwareType.Cpu:
                        if (_cpu == null) _cpu = hardware;
                        break;

                    case HardwareType.GpuNvidia:
                    case HardwareType.GpuAmd:
                    case HardwareType.GpuIntel:
                        // S'il y en a plusieurs (puce intégrée + carte dédiée), on garde celle qui a le plus de mémoire vidéo.
                        float memory = Value(hardware, SensorType.SmallData, "GPU Memory Total", "D3D Dedicated Memory Total");
                        float score = (float.IsNaN(memory) ? 0f : memory) + (hardware.HardwareType == HardwareType.GpuIntel ? 0f : 1f);
                        if (score > bestScore)
                        {
                            bestScore = score;
                            _gpu = hardware;
                        }
                        break;
                }
            }
        }

        public string CpuName => _cpu?.Name ?? Loc.T("processeur non détecté", "processor not detected");
        public string GpuName => _gpu?.Name ?? Loc.T("carte graphique non détectée", "graphics card not detected");

        public Readings Read()
        {
            var r = new Readings { CpuName = CpuName, GpuName = GpuName };

            if (_cpu != null)
            {
                Update(_cpu);
                r.CpuLoad = Value(_cpu, SensorType.Load, "CPU Total");
                if (float.IsNaN(r.CpuLoad)) r.CpuLoad = Aggregate(_cpu, SensorType.Load, s => true, values => values.Average());

                // Selon le processeur : température du boîtier (Intel), Tctl/Tdie (AMD), sinon le cœur le plus chaud.
                r.CpuTemp = Value(_cpu, SensorType.Temperature,
                    "CPU Package", "Core (Tctl/Tdie)", "Core (Tdie)", "Core (Tctl)", "Core Max", "CCDs Max (Tdie)", "Core Average");
                if (float.IsNaN(r.CpuTemp))
                    r.CpuTemp = Aggregate(_cpu, SensorType.Temperature, s => !s.Name.Contains("Distance"), values => values.Max());

                // Fréquence du cœur le plus rapide à cet instant ("CPU Core #1", "P-Core #1", "Core #1"...).
                r.CpuMHz = Aggregate(_cpu, SensorType.Clock,
                    s => s.Name.Contains("Core #") && !s.Name.Contains("Effective"), values => values.Max());
                if (float.IsNaN(r.CpuMHz)) r.CpuMHz = Value(_cpu, SensorType.Clock, "Cores (Average)");
            }

            if (_gpu != null)
            {
                Update(_gpu);
                r.GpuLoad = Value(_gpu, SensorType.Load, "GPU Core", "D3D 3D");
                r.GpuTemp = Value(_gpu, SensorType.Temperature, "GPU Core", "GPU Hot Spot");
                if (float.IsNaN(r.GpuTemp)) r.GpuTemp = Aggregate(_gpu, SensorType.Temperature, s => true, values => values.Max());
                r.GpuMHz = Value(_gpu, SensorType.Clock, "GPU Core");
                r.VramUsedMB = Value(_gpu, SensorType.SmallData, "GPU Memory Used", "D3D Dedicated Memory Used");
                r.VramTotalMB = Value(_gpu, SensorType.SmallData, "GPU Memory Total", "D3D Dedicated Memory Total");
            }

            return r;
        }

        private static void Update(IHardware hardware)
        {
            hardware.Update();
            foreach (IHardware sub in hardware.SubHardware) sub.Update();
        }

        private static IEnumerable<ISensor> Sensors(IHardware hardware)
        {
            foreach (ISensor sensor in hardware.Sensors) yield return sensor;
            foreach (IHardware sub in hardware.SubHardware)
                foreach (ISensor sensor in sub.Sensors) yield return sensor;
        }

        /// <summary>Premier capteur disponible parmi les noms donnés, par ordre de préférence.</summary>
        private static float Value(IHardware hardware, SensorType type, params string[] names)
        {
            foreach (string name in names)
            {
                foreach (ISensor sensor in Sensors(hardware))
                {
                    if (sensor.SensorType == type && sensor.Name == name && sensor.Value.HasValue && !float.IsNaN(sensor.Value.Value))
                        return sensor.Value.Value;
                }
            }
            return float.NaN;
        }

        private static float Aggregate(IHardware hardware, SensorType type, Func<ISensor, bool> filter, Func<IEnumerable<float>, float> combine)
        {
            List<float> values = Sensors(hardware)
                .Where(s => s.SensorType == type && s.Value.HasValue && !float.IsNaN(s.Value.Value) && filter(s))
                .Select(s => s.Value.Value)
                .ToList();
            return values.Count > 0 ? combine(values) : float.NaN;
        }

        public void Dispose()
        {
            _computer.Close();
        }
    }

    /// <summary>Icône dans la zone de notification, fil de lecture des capteurs, et services de la fenêtre principale.</summary>
    internal sealed class TrayApp : ApplicationContext
    {
        private const int PeriodMs = 500;

        private readonly NotifyIcon _icon;
        private readonly System.Windows.Forms.Timer _uiTimer;
        private readonly Thread _worker;
        private readonly ManualResetEventSlim _stop = new ManualResetEventSlim(false);
        private volatile Readings _latest;
        private volatile string _error;

        // Fenêtre miroir du panneau sur le bureau (créée à la demande).
        private readonly Settings _settings;
        private ContextMenuStrip _menu;
        private ToolStripMenuItem _panelItem;
        private PanelShare _panelShare;
        private PanelWindow _panelWindow;

        // Réglages de l'overlay : lus et écrits dans XRPerfOverlay.ini (dossier parent de ce programme),
        // envoyés en direct à la layer, et illustrés par l'aperçu hors jeu.
        private readonly string _iniPath;
        private readonly string _layerDll;
        private readonly string _layerManifest;  // fichier .json de la layer : nom de son inscription OpenXR
        private bool _layerBusy;                 // activation ou désactivation en cours (invite UAC ouverte)
        private ToolStripMenuItem _layerItem;
        private readonly string _settingsUnavailable; // non nul : pourquoi les réglages du casque ne sont pas modifiables
        private OverlaySettings _overlay;
        // Réglages tels que lus dans le .ini ou enregistrés pour la dernière fois : sert à savoir lesquels
        // ont été changés ici, et lesquels l'ont été en jeu par les raccourcis (à reprendre du .ini).
        private OverlaySettings _baseline;
        private readonly SettingsShare _settingsShare;
        private readonly OverlayPreview _preview;
        private MainWindow _main;
        private readonly string _installFolder;
        private readonly string _layerVersion;
        private readonly System.Windows.Forms.Timer _saveTimer;
        // Réglages du PC (XRPerfSensors.ini) changés dans l'interface : écrits une fois le curseur relâché,
        // pas à chaque pas d'un glissement.
        private readonly System.Windows.Forms.Timer _desktopSaveTimer;
        // XRPerfVR : panneau, montre et rapports dans les jeux SteamVR (OpenVR).
        private readonly VrCompanion _vr;
        // Fenêtre du bureau capturée pour le casque (null si la mémoire partagée n'a pas pu être créée).
        private readonly WindowCapture _capture;
        private bool _overlayDirty;
        private bool _saveErrorShown;

        public TrayApp()
        {
            _settings = Settings.Load();
            Loc.Set(_settings.Language);

            // L'installation range ce programme dans <dossier>\sensors, à côté de la DLL et de son .ini.
            string exeFolder = AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            string installFolder = Path.GetDirectoryName(exeFolder) ?? exeFolder;
            _installFolder = installFolder;
            _layerDll = Path.Combine(installFolder, "XR_APILAYER_NOVENDOR_perf_overlay.dll");
            _layerManifest = Path.Combine(installFolder, LayerRegistry.ManifestName);
            _iniPath = Path.Combine(installFolder, "XRPerfOverlay.ini");
            if (!File.Exists(_layerDll))
            {
                _settingsUnavailable = Loc.T("XRPerfSensors n'est pas dans le sous-dossier « sensors » d'une installation de XR Perf Overlay (DLL introuvable dans ",
                                             "XRPerfSensors is not in the « sensors » subfolder of an XR Perf Overlay installation (DLL not found in ") + installFolder + ").";
            }
            _overlay = _settingsUnavailable == null ? OverlaySettings.Load(_iniPath) : new OverlaySettings();
            // La langue choisie ici vaut aussi pour le panneau et les rapports : le .ini la reçoit si elle diffère.
            bool languageDiffers = _overlay.Language != Loc.IndexOf(_settings.Language);
            _overlay.Language = Loc.IndexOf(_settings.Language);
            if (languageDiffers && _settingsUnavailable == null) _overlay.Save(_iniPath);
            _baseline = _overlay.Clone();
            _preview = new OverlayPreview(_layerDll);
            try
            {
                _settingsShare = new SettingsShare();
                if (_settingsUnavailable == null) _settingsShare.Publish(_overlay, false);
            }
            catch (Exception)
            {
                _settingsShare = null; // les réglages restent enregistrés dans le .ini, pris en compte au prochain lancement d'un jeu
            }
            _saveTimer = new System.Windows.Forms.Timer { Interval = 400 };
            _saveTimer.Tick += (s, e) => FlushOverlaySave();
            _desktopSaveTimer = new System.Windows.Forms.Timer { Interval = 400 };
            _desktopSaveTimer.Tick += (s, e) => FlushDesktopSave();

            _icon = new NotifyIcon
            {
                // Nom complet : LibreHardwareMonitor a aussi une classe "SystemInformation".
                Icon = AppIcon.Load(System.Windows.Forms.SystemInformation.SmallIconSize),
                Text = "XRPerfSensors " + AppVersion.Text,
                Visible = true,
            };
            BuildMenu();
            _icon.DoubleClick += (s, e) => ShowMain(null);

            // DLL et programme d'installations différentes (copie à la main d'une partie seulement) : le
            // panneau du bureau et les réglages en direct peuvent ne pas fonctionner.
            _layerVersion = _settingsUnavailable == null ? AppVersion.OfFile(_layerDll) : "";
            if (_layerVersion.Length > 0 && AppVersion.Text != "dev" && _layerVersion != AppVersion.Text)
            {
                _icon.ShowBalloonTip(10000, "XRPerfSensors",
                    string.Format(Loc.T("La layer installée (version {0}) et XRPerfSensors (version {1}) ne viennent pas de la même version. Relancez Installer.bat pour tout mettre à jour.",
                                        "The installed layer (version {0}) and XRPerfSensors (version {1}) come from different versions. Run Installer.bat again to update everything."),
                                  _layerVersion, AppVersion.Text), ToolTipIcon.Warning);
            }
            else if (!LibreHardwareMonitor.PawnIo.PawnIo.IsInstalled)
            {
                _icon.ShowBalloonTip(10000, "XRPerfSensors",
                    Loc.T("Le pilote PawnIO n'est pas installé : la température et la fréquence du processeur seront indisponibles. " +
                          "Installez-le depuis pawnio.eu (ou : winget install namazso.PawnIO) puis relancez ce programme.",
                          "The PawnIO driver is not installed: CPU temperature and clock will be unavailable. " +
                          "Install it from pawnio.eu (or: winget install namazso.PawnIO) then restart this program."),
                    ToolTipIcon.Warning);
            }

            _worker = new Thread(WorkerLoop) { IsBackground = true, Name = "Capteurs" };
            _worker.Start();

            if (_settings.DesktopPanel) SetDesktopPanel(true);

            _vr = new VrCompanion(exeFolder);
            UpdateVrCompanion();
            try
            {
                _capture = new WindowCapture(exeFolder);
            }
            catch (Exception)
            {
                _capture = null;
            }

            _uiTimer = new System.Windows.Forms.Timer { Interval = 1000 };
            _uiTimer.Tick += (s, e) =>
            {
                RefreshTooltip();
                UpdateVrCompanion(); // relance XRPerfVR s'il s'est arrêté
                UpdateCapture();     // capture de fenêtre demandée (ou plus) par un jeu VR
            };
            _uiTimer.Start();
        }

        // --- Services de la fenêtre principale -----------------------------------------------------------

        public OverlaySettings Overlay { get { return _overlay; } }
        public Settings Desktop { get { return _settings; } }
        /// <summary>Non nul : les réglages du casque ne peuvent pas être enregistrés, et pourquoi.</summary>
        public string Unavailable { get { return _settingsUnavailable; } }
        public WindowCapture Capture { get { return _capture; } }
        public Readings Latest { get { return _latest; } }
        public string SensorError { get { return _error; } }
        public string LayerVersion { get { return _layerVersion; } }
        /// <summary>Fichier .json de la layer de cette installation (nom de son inscription OpenXR).</summary>
        public string LayerManifest { get { return _layerManifest; } }
        /// <summary>L'aperçu du panneau est possible (DLL de la layer présente).</summary>
        public bool PreviewAvailable { get { return File.Exists(_layerDll); } }

        /// <summary>Aperçu du panneau (RGBA, 640 x 320) avec les réglages et les capteurs actuels ; hauteur dessinée, 0 si indisponible.</summary>
        public int RenderPreview(byte[] pixels)
        {
            return _preview.Render(_overlay, _latest, pixels);
        }

        /// <summary>Aperçu de la montre (RGBA) ; côté de l'image, 0 si indisponible.</summary>
        public int RenderWatch(byte[] pixels)
        {
            return _preview.RenderWatch(_overlay, pixels);
        }

        /// <summary>Lecture de l'image du panneau publiée par un jeu (la layer ne la publie que tant que quelqu'un la lit).</summary>
        public PanelShare PanelReader()
        {
            try
            {
                if (_panelShare == null) _panelShare = new PanelShare();
                _panelShare.Heartbeat();
                return _panelShare;
            }
            catch (Exception)
            {
                return null;
            }
        }

        /// <summary>La fenêtre principale ne lit plus l'image : la layer peut arrêter de la publier si le miroir est fermé.</summary>
        public void PanelReaderIdle()
        {
            if (_panelShare != null && (_panelWindow == null || !_panelWindow.Visible)) _panelShare.StopWatching();
        }

        /// <summary>Ouvre la fenêtre principale (ou la ramène au premier plan), sur une page donnée.</summary>
        public void ShowMain(string page)
        {
            try
            {
                // Relecture du .ini : il a pu changer en jeu (raccourcis clavier). Un enregistrement en
                // attente passe d'abord, pour ne pas relire une version périmée.
                if (_main == null || _main.IsDisposed)
                {
                    ReloadOverlayFromIni();
                    _main = new MainWindow(this, page);
                    _main.FormClosed += (s, e) =>
                    {
                        // Fermée : détruite, pour libérer le moteur WebView2. La capture n'a plus d'aperçu à montrer.
                        // (Une fenêtre non modale est détruite par sa fermeture.)
                        _main = null;
                        PanelReaderIdle();
                        UpdateCapture();
                    };
                    _main.Show();
                }
                else
                {
                    if (page != null) _main.Navigate(page);
                    if (_main.WindowState == FormWindowState.Minimized) _main.WindowState = FormWindowState.Normal;
                    _main.Show();
                }
                _main.Activate();
            }
            catch (Exception ex)
            {
                _icon.ShowBalloonTip(8000, "XRPerfSensors", Loc.T("Impossible d'ouvrir la fenêtre : ", "Could not open the window: ") + ex.Message, ToolTipIcon.Error);
            }
        }

        /// <summary>
        /// Relit le .ini (il a pu changer en jeu, par les raccourcis clavier), sauf si un changement fait ici
        /// attend encore d'y être écrit. Retourne true si les réglages ont été relus.
        /// </summary>
        public bool ReloadOverlayFromIni()
        {
            FlushOverlaySave();
            if (_settingsUnavailable != null || _overlayDirty) return false;
            int language = _overlay.Language;
            _overlay = OverlaySettings.Load(_iniPath);
            _overlay.Language = language;
            _baseline = _overlay.Clone();
            return true;
        }

        // Dossiers où la layer écrit les rapports : celui du .ini (par défaut "sessions" à côté de la DLL),
        // et celui de secours quand il n'est pas accessible en écriture.
        public IList<string> ReportFolders()
        {
            var folders = new List<string>();
            string configured = _settingsUnavailable == null ? OverlaySettings.Load(_iniPath).SummaryFolder : "";
            // Un chemin relatif dépendrait du dossier de travail du jeu : seul un chemin complet est suivi.
            folders.Add(configured.Length > 0 && Path.IsPathRooted(configured) ? configured : Path.Combine(_installFolder, "sessions"));
            folders.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "XRPerfOverlay", "sessions"));
            return folders.Distinct(StringComparer.OrdinalIgnoreCase).Where(Directory.Exists).ToList();
        }

        // Un réglage du casque a changé : envoi immédiat à la layer (jeu en cours), puis enregistrement dans
        // le .ini après un court délai, pour ne pas l'écrire à chaque cran d'un curseur.
        /// <summary>État de l'inscription de la layer auprès d'OpenXR (lu dans le registre).</summary>
        public LayerRegistry.State LayerState { get { return LayerRegistry.Read(_layerManifest); } }
        public bool LayerBusy { get { return _layerBusy; } }

        /// <summary>
        /// Active ou désactive la layer dans les jeux OpenXR (pris en compte au prochain lancement d'un
        /// jeu). Un refus ou un échec est signalé dans la fenêtre si elle est ouverte, sinon par une bulle.
        /// </summary>
        public async void SetLayerEnabled(bool enabled)
        {
            if (_layerBusy) return;
            _layerBusy = true;
            LayerChanged();
            LayerRegistry.Outcome outcome;
            try
            {
                outcome = await LayerRegistry.SetAsync(enabled, _layerManifest);
            }
            catch (Exception)
            {
                outcome = LayerRegistry.Outcome.Failed;
            }
            _layerBusy = false;
            LayerChanged();
            if (outcome == LayerRegistry.Outcome.Done) return;
            string text = outcome == LayerRegistry.Outcome.Cancelled
                ? Loc.T("Autorisation refusée : la layer n'a pas été modifiée.", "Permission refused: the layer was not changed.")
                : outcome == LayerRegistry.Outcome.Missing
                ? Loc.T("La layer n'est pas inscrite auprès d'OpenXR : relancez Installer.bat.", "The layer is not registered with OpenXR: run Installer.bat again.")
                : Loc.T("La layer n'a pas pu être modifiée (registre inaccessible).", "The layer could not be changed (registry not accessible).");
            if (_main != null && !_main.IsDisposed && _main.Visible) _main.PostToast(text);
            else _icon.ShowBalloonTip(6000, "XRPerfSensors", text, ToolTipIcon.Warning);
        }

        private void LayerChanged()
        {
            LayerRegistry.State state = LayerState;
            if (_layerItem != null)
            {
                _layerItem.Checked = state == LayerRegistry.State.Enabled;
                _layerItem.Enabled = state != LayerRegistry.State.Missing && !_layerBusy;
            }
            if (_main != null && !_main.IsDisposed) _main.PostLayer();
        }

        public void OverlayChanged()
        {
            if (_settingsUnavailable != null) return;
            if (_settingsShare != null) _settingsShare.Publish(_overlay, true);
            UpdateVrCompanion();
            UpdateCapture();
            _overlayDirty = true;
            _saveTimer.Stop();
            _saveTimer.Start();
        }

        /// <summary>Panneau et apparence remis aux valeurs d'origine (la montre, la fenêtre et les mesures ne changent pas).</summary>
        public void ResetOverlayDefaults()
        {
            if (_settingsUnavailable != null) return;
            var fresh = new OverlaySettings();
            OverlaySettings o = _overlay;
            o.Visible = fresh.Visible; o.Position = fresh.Position; o.Distance = fresh.Distance; o.OffsetX = fresh.OffsetX; o.OffsetY = fresh.OffsetY;
            o.Width = fresh.Width; o.Opacity = fresh.Opacity; o.GlobalOpacity = fresh.GlobalOpacity; o.FaceViewer = fresh.FaceViewer;
            o.Clock = fresh.Clock; o.HeadsetInfo = fresh.HeadsetInfo; o.PanelSession = fresh.PanelSession; o.PanelBattery = fresh.PanelBattery;
            o.Layout = fresh.Layout; o.Theme = fresh.Theme;
            o.CustomBackground = fresh.CustomBackground; o.CustomText = fresh.CustomText; o.CustomAccent = fresh.CustomAccent;
            o.Anchor = fresh.Anchor; o.HandPlacement = fresh.HandPlacement; o.GazeOnly = fresh.GazeOnly; o.HandAlong = fresh.HandAlong; o.HandLift = fresh.HandLift;
            OverlayChanged();
        }

        /// <summary>La langue a changé : menu, fenêtre miroir, panneau du casque et rapports suivent.</summary>
        public void LanguageChanged()
        {
            Loc.Set(_settings.Language);
            BuildMenu();
            if (_panelWindow != null && !_panelWindow.IsDisposed) _panelWindow.Invalidate();
            if (_settingsUnavailable == null)
            {
                _overlay.Language = Loc.IndexOf(_settings.Language);
                OverlayChanged();
            }
        }

        // XRPerfVR tourne si l'option est active (et si l'installation est complète).
        private void UpdateVrCompanion()
        {
            bool wanted = _settingsUnavailable == null && _overlay.OpenVR;
            bool gaveUp = _vr.GaveUp;
            _vr.Update(wanted);
            if (!gaveUp && _vr.GaveUp)
            {
                _icon.ShowBalloonTip(8000, "XRPerfSensors",
                    Loc.T("XRPerfVR (panneau dans les jeux SteamVR) s'arrête sans cesse : il n'est plus relancé. Le journal XRPerfOverlay.log, " +
                          "dans %LOCALAPPDATA%\\XRPerfOverlay, en donne la cause.",
                          "XRPerfVR (panel in SteamVR games) keeps stopping: it is no longer restarted. The XRPerfOverlay.log file, " +
                          "in %LOCALAPPDATA%\\XRPerfOverlay, gives the reason."), ToolTipIcon.Warning);
            }
        }

        // Capture de fenêtre : lancée tant qu'un jeu VR affiche la fenêtre, ou que son aperçu est à l'écran.
        public void UpdateCapture()
        {
            if (_capture == null) return;
            bool preview = _main != null && !_main.IsDisposed && _main.WindowPreviewShown;
            try
            {
                _capture.Update(_settingsUnavailable == null && _overlay.WindowEnabled, _settings, preview);
            }
            catch (Exception)
            {
                // Bibliothèque de capture défaillante : la fenêtre n'est simplement pas affichée.
            }
        }

        /// <summary>La fenêtre choisie ou les réglages de capture ont changé.</summary>
        public void CaptureChanged()
        {
            SaveDesktopSoon();
            UpdateCapture();
        }

        /// <summary>Enregistre les réglages du PC un peu plus tard (regroupe les changements rapprochés).</summary>
        public void SaveDesktopSoon()
        {
            _desktopSaveDirty = true;
            _desktopSaveTimer.Stop();
            _desktopSaveTimer.Start();
        }

        private bool _desktopSaveDirty;

        private void FlushDesktopSave()
        {
            _desktopSaveTimer.Stop();
            if (!_desktopSaveDirty) return;
            _desktopSaveDirty = false;
            SavePanelPosition();
        }

        private void FlushOverlaySave()
        {
            _saveTimer.Stop();
            if (!_overlayDirty || _settingsUnavailable != null) return;
            _overlayDirty = false;
            // Raccourcis utilisés en jeu pendant que la fenêtre restait ouverte : leur effet, déjà dans le
            // .ini, n'est pas écrasé par l'état affiché ici.
            if (_overlay.MergeHotkeyState(OverlaySettings.Load(_iniPath), _baseline))
            {
                if (_settingsShare != null) _settingsShare.Publish(_overlay, false);
                if (_main != null && !_main.IsDisposed) _main.OverlayReloaded();
            }
            _baseline = _overlay.Clone();
            if (!_overlay.Save(_iniPath) && !_saveErrorShown)
            {
                _saveErrorShown = true;
                _icon.ShowBalloonTip(8000, "XRPerfSensors", Loc.T("Impossible d'enregistrer les réglages dans ", "Could not save the settings to ") + _iniPath, ToolTipIcon.Warning);
            }
        }

        // --- Menu de l'icône et fenêtre miroir -------------------------------------------------------------

        private void BuildMenu()
        {
            var menu = new ContextMenuStrip();
            menu.Items.Add(new ToolStripMenuItem("XRPerfSensors " + AppVersion.Text) { Enabled = false });
            menu.Items.Add(new ToolStripSeparator());
            var open = new ToolStripMenuItem(Loc.T("Ouvrir XR Perf Overlay", "Open XR Perf Overlay"), null, (s, e) => ShowMain("dashboard"));
            open.Font = new Font(open.Font, FontStyle.Bold);
            menu.Items.Add(open);
            menu.Items.Add(Loc.T("Rapports de session", "Session reports"), null, (s, e) => ShowMain("reports"));
            _panelItem = new ToolStripMenuItem(Loc.T("Miroir du panneau sur le bureau (OBS)", "Panel mirror on the desktop (OBS)")) { CheckOnClick = true, Checked = _settings.DesktopPanel };
            _panelItem.Click += (s, e) => SetDesktopPanel(_panelItem.Checked);
            menu.Items.Add(_panelItem);
            // Layer OpenXR : désactivée, elle n'est plus chargée du tout dans les jeux OpenXR.
            _layerItem = new ToolStripMenuItem(Loc.T("Layer OpenXR activée", "OpenXR layer enabled"));
            _layerItem.Click += (s, e) => SetLayerEnabled(!_layerItem.Checked);
            menu.Items.Add(_layerItem);
            menu.Opening += (s, e) => LayerChanged();
            if (!LibreHardwareMonitor.PawnIo.PawnIo.IsInstalled)
            {
                menu.Items.Add(Loc.T("Pilote PawnIO absent : installer (température / fréquence CPU)...", "PawnIO driver missing: install it (CPU temperature / clock)..."), null,
                    (s, e) => OpenUrl("https://pawnio.eu/"));
            }
            menu.Items.Add(new ToolStripSeparator());
            menu.Items.Add(Loc.T("Quitter", "Exit"), null, (s, e) => ExitThread());
            ContextMenuStrip old = _menu;
            _menu = menu;
            _icon.ContextMenuStrip = menu;
            if (_panelWindow != null && !_panelWindow.IsDisposed) _panelWindow.ContextMenuStrip = menu;
            if (old != null) old.Dispose();
        }

        /// <summary>Affiche ou masque la fenêtre miroir, et mémorise le choix.</summary>
        private void SetDesktopPanel(bool show)
        {
            if (show)
            {
                try
                {
                    if (_panelShare == null) _panelShare = new PanelShare();
                    if (_panelWindow == null || _panelWindow.IsDisposed)
                    {
                        _panelWindow = new PanelWindow(_panelShare) { TopMost = _settings.TopMost, ContextMenuStrip = _menu };
                        // Sans jeu VR en cours, la fenêtre montre un aperçu des réglages actuels.
                        _panelWindow.SetPreviewSource(pixels => _preview.Render(_overlay, _latest, pixels));
                        // Position mémorisée, si elle tombe encore sur un écran branché.
                        var bounds = new Rectangle(_settings.X, _settings.Y, _panelWindow.Width, _panelWindow.Height);
                        bool onScreen = Screen.AllScreens.Any(screen => screen.WorkingArea.IntersectsWith(bounds));
                        _panelWindow.Location = onScreen ? bounds.Location : new Point(60, 60);
                        _panelWindow.SetAppearance(_settings.Scale, _settings.Opacity, _settings.Anchor);
                        // Fenêtre déplacée à la souris : elle repasse en emplacement libre et sa position est mémorisée.
                        // (Un simple clic déclenche aussi ces deux événements, sans déplacement : il ne change rien.)
                        Point dragStart = Point.Empty;
                        _panelWindow.ResizeBegin += (s, e) => dragStart = _panelWindow.Location;
                        _panelWindow.ResizeEnd += (s, e) =>
                        {
                            if (_settings.Anchor != 0 && _panelWindow.Location != dragStart)
                            {
                                _settings.Anchor = 0;
                                _panelWindow.SetAppearance(_settings.Scale, _settings.Opacity, _settings.Anchor);
                                if (_main != null && !_main.IsDisposed) _main.PostDesktop();
                            }
                            SavePanelPosition();
                        };
                        // Fenêtre masquée par Alt+F4 : la case du menu et celle de l'interface suivent.
                        _panelWindow.VisibleChanged += (s, e) =>
                        {
                            if (!_panelWindow.Visible && _panelItem.Checked)
                            {
                                _panelItem.Checked = false;
                                _settings.DesktopPanel = false;
                                _settings.Save();
                                if (_main != null && !_main.IsDisposed) _main.PostDesktop();
                            }
                        };
                    }
                    _panelWindow.Show();
                }
                catch (Exception ex)
                {
                    show = false;
                    _icon.ShowBalloonTip(8000, "XRPerfSensors", Loc.T("Impossible d'afficher le panneau sur le bureau : ", "Could not show the panel on the desktop: ") + ex.Message, ToolTipIcon.Error);
                }
            }
            else if (_panelWindow != null)
            {
                _panelWindow.Hide();
            }

            _panelItem.Checked = show;
            _settings.DesktopPanel = show;
            _settings.Save();
            if (_main != null && !_main.IsDisposed) _main.PostDesktop();
        }

        /// <summary>Un réglage de la fenêtre miroir a changé dans l'interface.</summary>
        public void DesktopChanged()
        {
            bool shown = _panelWindow != null && _panelWindow.Visible;
            if (_settings.DesktopPanel != shown) SetDesktopPanel(_settings.DesktopPanel);
            if (_panelWindow != null && !_panelWindow.IsDisposed)
            {
                // Seulement s'il change : cette propriété ramène la fenêtre au premier plan à chaque affectation.
                if (_panelWindow.TopMost != _settings.TopMost) _panelWindow.TopMost = _settings.TopMost;
                _panelWindow.SetAppearance(_settings.Scale, _settings.Opacity, _settings.Anchor);
            }
            SaveDesktopSoon();
        }

        private void SavePanelPosition()
        {
            if (_panelWindow != null && !_panelWindow.IsDisposed)
            {
                _settings.X = _panelWindow.Left;
                _settings.Y = _panelWindow.Top;
            }
            _settings.Save();
        }

        private void WorkerLoop()
        {
            try
            {
                using (var block = new SharedBlock())
                using (var source = new SensorSource())
                {
                    while (!_stop.IsSet)
                    {
                        Readings readings = source.Read();
                        block.Write(readings);
                        _latest = readings;
                        _stop.Wait(PeriodMs);
                    }
                }
            }
            catch (Exception ex)
            {
                _error = ex.Message;
            }
        }

        private void RefreshTooltip()
        {
            string text;
            if (_error != null)
            {
                text = "XRPerfSensors - " + Loc.T("erreur : ", "error: ") + _error;
            }
            else
            {
                Readings r = _latest;
                text = r == null
                    ? "XRPerfSensors - " + Loc.T("démarrage...", "starting...")
                    : string.Format("XRPerfSensors {4}\nCPU {0} %  {1} °C\nGPU {2} %  {3} °C", Show(r.CpuLoad), Show(r.CpuTemp), Show(r.GpuLoad), Show(r.GpuTemp),
                                    AppVersion.Text);
            }
            // Le texte d'une icône de notification est limité à 63 caractères sur certaines versions de .NET.
            _icon.Text = text.Length > 63 ? text.Substring(0, 63) : text;
        }

        private static string Show(float value)
        {
            return float.IsNaN(value) ? "--" : Math.Round(value).ToString("0");
        }

        public static void OpenUrl(string url)
        {
            try
            {
                Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
            }
            catch (Exception)
            {
                // Pas de navigateur disponible : sans conséquence.
            }
        }

        protected override void ExitThreadCore()
        {
            _uiTimer.Stop(); // plus de relance de XRPerfVR à partir d'ici
            _vr.Dispose();   // rapport en cours terminé par XRPerfVR avant la fermeture
            if (_capture != null) _capture.Dispose();  // capture arrêtée avant la fin du programme
            _stop.Set();
            _worker.Join(3000);
            FlushOverlaySave();
            _saveTimer.Dispose();
            FlushDesktopSave();
            _desktopSaveTimer.Dispose();
            // La fenêtre principale est retirée d'abord, et oubliée : masquer la fenêtre miroir, plus bas,
            // ne doit plus chercher à la mettre à jour.
            MainWindow main = _main;
            _main = null;
            if (main != null && !main.IsDisposed)
            {
                main.Close();
                main.Dispose();
            }
            if (_settingsShare != null) _settingsShare.Dispose();
            if (_panelWindow != null)
            {
                SavePanelPosition();
                bool wasShown = _panelWindow.Visible;
                _panelWindow.Hide();          // déclenche VisibleChanged...
                _settings.DesktopPanel = wasShown;  // ...mais le choix de l'utilisateur est conservé pour le prochain lancement
                _settings.Save();
                _panelWindow.Dispose();
            }
            if (_panelShare != null) _panelShare.Dispose();
            _icon.Visible = false;
            _icon.Dispose();
            if (_menu != null) _menu.Dispose();
            base.ExitThreadCore();
        }
    }

    internal static class Program
    {
        [STAThread]
        private static int Main(string[] args)
        {
            // Instance relancée en administrateur pour activer ou désactiver la layer : une écriture, puis fin.
            if (args.Length == 2 && args[0] == LayerRegistry.CommandArgument) return LayerRegistry.RunCommand(args[1]);
            if (args.Length == 3 && args[0] == LayerRegistry.OtherArgument) return LayerRegistry.RunOtherCommand(args[1], args[2]);

            // Une seule instance à la fois.
            using (var single = new Mutex(true, @"Local\XRPerfOverlay.SensorsApp", out bool first))
            {
                if (!first) return 0;
                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);
                Application.Run(new TrayApp());
            }
            return 0;
        }
    }
}
