// XRPerfSensors : lit les capteurs du PC (LibreHardwareMonitor) et les publie deux fois par
// seconde dans une mémoire partagée que la layer OpenXR affiche dans le casque.
// Il peut aussi afficher le panneau dans une fenêtre du bureau (voir PanelWindow.cs).
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

        public string CpuName => _cpu?.Name ?? "processeur non détecté";
        public string GpuName => _gpu?.Name ?? "carte graphique non détectée";

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

    /// <summary>Icône dans la zone de notification + fil de lecture des capteurs.</summary>
    internal sealed class TrayApp : ApplicationContext
    {
        private const int PeriodMs = 500;

        private readonly NotifyIcon _icon;
        private readonly System.Windows.Forms.Timer _uiTimer;
        private readonly Thread _worker;
        private readonly ManualResetEventSlim _stop = new ManualResetEventSlim(false);
        private volatile Readings _latest;
        private volatile string _error;

        // Fenêtre du bureau (créée à la demande).
        private readonly Settings _settings;
        private readonly ContextMenuStrip _menu;
        private readonly ToolStripMenuItem _panelItem;
        private PanelShare _panelShare;
        private PanelWindow _panelWindow;

        // Réglages de l'overlay : lus et écrits dans XRPerfOverlay.ini (dossier parent de ce programme),
        // envoyés en direct à la layer, et illustrés par l'aperçu hors jeu.
        private readonly string _iniPath;
        private readonly string _settingsUnavailable; // non nul : pourquoi les réglages du casque ne sont pas modifiables
        private OverlaySettings _overlay;
        // Réglages tels que lus dans le .ini ou enregistrés pour la dernière fois : sert à savoir lesquels
        // ont été changés ici, et lesquels l'ont été en jeu par les raccourcis (à reprendre du .ini).
        private OverlaySettings _baseline;
        private readonly SettingsShare _settingsShare;
        private readonly OverlayPreview _preview;
        private SettingsWindow _settingsWindow;
        private ReportsWindow _reportsWindow;
        private readonly string _installFolder;
        private readonly System.Windows.Forms.Timer _saveTimer;
        // XRPerfVR : panneau, montre et rapports dans les jeux SteamVR (OpenVR).
        private readonly VrCompanion _vr;
        // Fenêtre du bureau capturée pour le casque (null si la mémoire partagée n'a pas pu être créée).
        private readonly WindowCapture _capture;
        private bool _overlayDirty;
        private bool _saveErrorShown;

        public TrayApp()
        {
            _settings = Settings.Load();

            var menu = new ContextMenuStrip();
            _menu = menu;
            menu.Items.Add(new ToolStripMenuItem("XRPerfSensors " + AppVersion.Text + " - capteurs pour XR Perf Overlay") { Enabled = false });

            // L'installation range ce programme dans <dossier>\sensors, à côté de la DLL et de son .ini.
            string exeFolder = AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            string installFolder = Path.GetDirectoryName(exeFolder) ?? exeFolder;
            _installFolder = installFolder;
            string layerDll = Path.Combine(installFolder, "XR_APILAYER_NOVENDOR_perf_overlay.dll");
            _iniPath = Path.Combine(installFolder, "XRPerfOverlay.ini");
            if (!File.Exists(layerDll))
            {
                _settingsUnavailable = "Réglages du casque indisponibles : XRPerfSensors n'est pas dans le sous-dossier « sensors » " +
                                       "d'une installation de XR Perf Overlay (DLL introuvable dans " + installFolder + ").";
            }
            _overlay = _settingsUnavailable == null ? OverlaySettings.Load(_iniPath) : new OverlaySettings();
            _baseline = _overlay.Clone();
            _preview = new OverlayPreview(layerDll);
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

            _panelItem = new ToolStripMenuItem("Panneau sur le bureau (aperçu, miroir pour OBS)") { CheckOnClick = true };
            _panelItem.Click += (s, e) => SetDesktopPanel(_panelItem.Checked);
            menu.Items.Add(new ToolStripSeparator());
            menu.Items.Add("Réglages...", null, (s, e) => ShowSettings());
            menu.Items.Add("Rapports de session...", null, (s, e) => ShowReports());
            menu.Items.Add(_panelItem);
            if (!LibreHardwareMonitor.PawnIo.PawnIo.IsInstalled)
            {
                menu.Items.Add("Pilote PawnIO absent : installer (température / fréquence CPU)...", null,
                    (s, e) => OpenUrl("https://pawnio.eu/"));
            }
            menu.Items.Add(new ToolStripSeparator());
            menu.Items.Add("Quitter", null, (s, e) => ExitThread());

            _icon = new NotifyIcon
            {
                // Nom complet : LibreHardwareMonitor a aussi une classe "SystemInformation".
                Icon = AppIcon.Load(System.Windows.Forms.SystemInformation.SmallIconSize),
                Text = "XRPerfSensors " + AppVersion.Text,
                ContextMenuStrip = menu,
                Visible = true,
            };
            _icon.DoubleClick += (s, e) => ShowSettings();

            // DLL et programme d'installations différentes (copie à la main d'une partie seulement) : le
            // panneau du bureau et les réglages en direct peuvent ne pas fonctionner.
            string layerVersion = _settingsUnavailable == null ? AppVersion.OfFile(layerDll) : "";
            if (layerVersion.Length > 0 && AppVersion.Text != "dev" && layerVersion != AppVersion.Text)
            {
                _icon.ShowBalloonTip(10000, "XRPerfSensors",
                    "La layer installée (version " + layerVersion + ") et XRPerfSensors (version " + AppVersion.Text + ") ne viennent pas de la même " +
                    "version. Relancez Installer.bat pour tout mettre à jour.", ToolTipIcon.Warning);
            }
            else if (!LibreHardwareMonitor.PawnIo.PawnIo.IsInstalled)
            {
                _icon.ShowBalloonTip(10000, "XRPerfSensors",
                    "Le pilote PawnIO n'est pas installé : la température et la fréquence du processeur seront indisponibles. " +
                    "Installez-le depuis pawnio.eu (ou : winget install namazso.PawnIO) puis relancez ce programme.",
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

        /// <summary>Affiche ou masque la fenêtre du bureau, et mémorise le choix.</summary>
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
                        // Sans jeu OpenXR en cours, la fenêtre montre un aperçu des réglages actuels.
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
                                if (_settingsWindow != null) _settingsWindow.LoadDesktop();
                            }
                            SavePanelPosition();
                        };
                        // Fenêtre masquée par Alt+F4 : la case du menu et celle des réglages suivent.
                        _panelWindow.VisibleChanged += (s, e) =>
                        {
                            if (!_panelWindow.Visible && _panelItem.Checked)
                            {
                                _panelItem.Checked = false;
                                _settings.DesktopPanel = false;
                                _settings.Save();
                                if (_settingsWindow != null) _settingsWindow.LoadDesktop();
                            }
                        };
                    }
                    _panelWindow.Show();
                }
                catch (Exception ex)
                {
                    show = false;
                    _icon.ShowBalloonTip(8000, "XRPerfSensors", "Impossible d'afficher le panneau sur le bureau : " + ex.Message, ToolTipIcon.Error);
                }
            }
            else if (_panelWindow != null)
            {
                _panelWindow.Hide();
            }

            _panelItem.Checked = show;
            _settings.DesktopPanel = show;
            _settings.Save();
            if (_settingsWindow != null) _settingsWindow.LoadDesktop();
        }

        /// <summary>Ouvre la fenêtre de réglages (menu de l'icône ou double-clic).</summary>
        private void ShowSettings()
        {
            try
            {
                // Relecture du .ini : il a pu changer en jeu (raccourcis clavier). Un enregistrement en
                // attente passe d'abord, pour ne pas relire une version périmée.
                FlushOverlaySave();
                if (_settingsUnavailable == null) _overlay = OverlaySettings.Load(_iniPath);
                _baseline = _overlay.Clone();
                if (_settingsWindow == null || _settingsWindow.IsDisposed)
                    _settingsWindow = new SettingsWindow(_overlay, _settings, OnOverlayChanged, OnDesktopChanged, OnSettingsActivated, ShowReports,
                        _settingsUnavailable, (overlay, pixels) => _preview.RenderWatch(overlay, pixels), _capture, UpdateCapture);
                else
                    _settingsWindow.LoadFrom(_overlay);
                _settingsWindow.Show();
                if (_settingsWindow.WindowState == FormWindowState.Minimized) _settingsWindow.WindowState = FormWindowState.Normal;
                _settingsWindow.Activate();
            }
            catch (Exception ex)
            {
                _icon.ShowBalloonTip(8000, "XRPerfSensors", "Impossible d'ouvrir les réglages : " + ex.Message, ToolTipIcon.Error);
            }
        }

        /// <summary>Ouvre la fenêtre des rapports de session.</summary>
        private void ShowReports()
        {
            try
            {
                if (_reportsWindow == null || _reportsWindow.IsDisposed) _reportsWindow = new ReportsWindow(ReportFolders, _settings);
                _reportsWindow.Show();
                if (_reportsWindow.WindowState == FormWindowState.Minimized) _reportsWindow.WindowState = FormWindowState.Normal;
                _reportsWindow.Activate();
                _reportsWindow.RefreshReports(false); // sans effet si l'activation vient de le faire
            }
            catch (Exception ex)
            {
                _icon.ShowBalloonTip(8000, "XRPerfSensors", "Impossible d'ouvrir les rapports : " + ex.Message, ToolTipIcon.Error);
            }
        }

        // Dossiers où la layer écrit les rapports : celui du .ini (par défaut "sessions" à côté de la DLL),
        // et celui de secours quand il n'est pas accessible en écriture.
        private IList<string> ReportFolders()
        {
            var folders = new List<string>();
            string configured = _settingsUnavailable == null ? OverlaySettings.Load(_iniPath).SummaryFolder : "";
            // Un chemin relatif dépendrait du dossier de travail du jeu : seul un chemin complet est suivi.
            folders.Add(configured.Length > 0 && Path.IsPathRooted(configured) ? configured : Path.Combine(_installFolder, "sessions"));
            folders.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "XRPerfOverlay", "sessions"));
            return folders.Distinct(StringComparer.OrdinalIgnoreCase).Where(Directory.Exists).ToList();
        }

        // La fenêtre de réglages revient au premier plan : le .ini a pu changer entre-temps en jeu
        // (raccourcis clavier). On le relit, sauf si un changement fait ici attend encore d'y être écrit.
        private void OnSettingsActivated()
        {
            if (_settingsUnavailable != null || _overlayDirty || _settingsWindow == null || _settingsWindow.IsDisposed) return;
            _overlay = OverlaySettings.Load(_iniPath);
            _baseline = _overlay.Clone();
            _settingsWindow.LoadFrom(_overlay);
        }

        // Un réglage du casque a changé dans la fenêtre : envoi immédiat à la layer (jeu en cours),
        // puis enregistrement dans le .ini après un court délai, pour ne pas l'écrire à chaque cran d'un curseur.
        private void OnOverlayChanged()
        {
            if (_settingsUnavailable != null) return;
            if (_settingsShare != null) _settingsShare.Publish(_overlay, true);
            UpdateVrCompanion();
            UpdateCapture();
            _overlayDirty = true;
            _saveTimer.Stop();
            _saveTimer.Start();
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
                    "XRPerfVR (panneau dans les jeux SteamVR) s'arrête sans cesse : il n'est plus relancé. Le journal XRPerfOverlay.log, " +
                    "dans %LOCALAPPDATA%\\XRPerfOverlay, en donne la cause.", ToolTipIcon.Warning);
            }
        }

        // Capture de fenêtre : lancée tant qu'un jeu VR affiche la fenêtre, ou que son aperçu est à l'écran.
        private void UpdateCapture()
        {
            if (_capture == null) return;
            bool preview = _settingsWindow != null && !_settingsWindow.IsDisposed && _settingsWindow.WindowPreviewShown;
            try
            {
                _capture.Update(_settingsUnavailable == null && _overlay.WindowEnabled, _settings, preview);
            }
            catch (Exception)
            {
                // Bibliothèque de capture défaillante : la fenêtre n'est simplement pas affichée.
            }
        }

        private void FlushOverlaySave()
        {
            _saveTimer.Stop();
            if (!_overlayDirty || _settingsUnavailable != null) return;
            _overlayDirty = false;
            // Raccourcis utilisés en jeu pendant que la fenêtre de réglages restait ouverte : leur effet, déjà
            // dans le .ini, n'est pas écrasé par l'état affiché ici.
            if (_overlay.MergeHotkeyState(OverlaySettings.Load(_iniPath), _baseline))
            {
                if (_settingsShare != null) _settingsShare.Publish(_overlay, false);
                if (_settingsWindow != null && !_settingsWindow.IsDisposed) _settingsWindow.LoadFrom(_overlay);
            }
            _baseline = _overlay.Clone();
            if (!_overlay.Save(_iniPath) && !_saveErrorShown)
            {
                _saveErrorShown = true;
                _icon.ShowBalloonTip(8000, "XRPerfSensors", "Impossible d'enregistrer les réglages dans " + _iniPath, ToolTipIcon.Warning);
            }
        }

        // Un réglage de la fenêtre du bureau a changé dans la fenêtre de réglages.
        private void OnDesktopChanged()
        {
            bool shown = _panelWindow != null && _panelWindow.Visible;
            if (_settings.DesktopPanel != shown) SetDesktopPanel(_settings.DesktopPanel);
            if (_panelWindow != null && !_panelWindow.IsDisposed)
            {
                // Seulement s'il change : cette propriété ramène la fenêtre au premier plan à chaque affectation.
                if (_panelWindow.TopMost != _settings.TopMost) _panelWindow.TopMost = _settings.TopMost;
                _panelWindow.SetAppearance(_settings.Scale, _settings.Opacity, _settings.Anchor);
            }
            SavePanelPosition();
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
                text = "XRPerfSensors - erreur : " + _error;
            }
            else
            {
                Readings r = _latest;
                text = r == null
                    ? "XRPerfSensors - démarrage..."
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

        private static void OpenUrl(string url)
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
            // La fenêtre de réglages est retirée d'abord, et oubliée : masquer la fenêtre du bureau,
            // plus bas, ne doit plus chercher à la mettre à jour.
            SettingsWindow settingsWindow = _settingsWindow;
            _settingsWindow = null;
            if (settingsWindow != null) settingsWindow.Dispose();
            if (_reportsWindow != null) _reportsWindow.Dispose();
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
            base.ExitThreadCore();
        }
    }

    internal static class Program
    {
        [STAThread]
        private static void Main()
        {
            // Une seule instance à la fois.
            using (var single = new Mutex(true, @"Local\XRPerfOverlay.SensorsApp", out bool first))
            {
                if (!first) return;
                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);
                Application.Run(new TrayApp());
            }
        }
    }
}
