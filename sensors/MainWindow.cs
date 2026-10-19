// Fenêtre principale de XRPerfSensors : l'interface (dossier ui, HTML) affichée par WebView2, le moteur
// d'Edge présent sur Windows 10 et 11. Ce fichier fait le lien entre la page et le programme : messages
// JSON dans les deux sens, images d'aperçu, capteurs, rapports, plein écran.
//
// La fenêtre est détruite quand on la ferme (le programme continue dans la zone de notification) : le
// moteur WebView2 et sa mémoire sont alors libérés.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Windows.Forms;
using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;

namespace XRPerfSensors
{
    internal sealed class MainWindow : Form
    {
        private const string HostName = "xrperf.ui";
        private static readonly string[] PanelPages = { "dashboard", "panel", "appearance", "desktop" };

        private readonly TrayApp _app;
        private readonly ReportStore _reports;
        private WebView2 _web;
        private bool _pageReady;            // la page a demandé ses données (message "ready")
        private string _page;               // page affichée dans l'interface
        private readonly Timer _fast = new Timer { Interval = 250 };   // aperçus
        private readonly Timer _slow = new Timer { Interval = 1000 };  // capteurs, état, rapports
        private int _slowTicks;

        // Aperçu du panneau : image du jeu en cours (mémoire partagée) ou dessin de la layer.
        private readonly byte[] _panelPixels = new byte[PanelShare.PixelBytes];
        private readonly byte[] _watchPixels = new byte[OverlayPreview.WatchSize * OverlayPreview.WatchSize * 4];
        private int _panelSequence = -1;
        private int _gameState = PanelShare.StateNone;
        private string _lastStatus = "";

        // Plein écran.
        private bool _fullscreen;
        private FormWindowState _restoreState;
        private Rectangle _restoreBounds;

        public MainWindow(TrayApp app, string page)
        {
            _app = app;
            _page = page ?? "dashboard";
            _reports = new ReportStore(app.ReportFolders);
            Text = "XR Perf Overlay";
            Icon = AppIcon.Load(new Size(32, 32));
            BackColor = Color.FromArgb(14, 17, 22);
            MinimumSize = new Size(760, 540);
            StartPosition = FormStartPosition.Manual;
            KeyPreview = true;

            // Taille et position mémorisées, si elles tombent encore sur un écran branché.
            Settings s = app.Desktop;
            var bounds = new Rectangle(s.WinX, s.WinY, Math.Max(MinimumSize.Width, s.WinWidth), Math.Max(MinimumSize.Height, s.WinHeight));
            bool onScreen = s.WinX != int.MinValue && Screen.AllScreens.Any(screen => screen.WorkingArea.IntersectsWith(bounds));
            if (onScreen) Bounds = bounds;
            else
            {
                Rectangle area = Screen.PrimaryScreen.WorkingArea;
                Size = new Size(Math.Min(bounds.Width, area.Width - 40), Math.Min(bounds.Height, area.Height - 40));
                Location = new Point(area.Left + (area.Width - Width) / 2, area.Top + (area.Height - Height) / 2);
            }
            if (s.WinMaximized) WindowState = FormWindowState.Maximized;

            _fast.Tick += (sender, e) => FastTick();
            _slow.Tick += (sender, e) => SlowTick();
            Shown += (sender, e) =>
            {
                if (_app.Desktop.UiStartFullscreen) SetFullscreen(true);
                InitWebView();
            };
            // Retour au premier plan : le .ini a pu changer en jeu (raccourcis clavier). La page n'est
            // prévenue que si quelque chose a vraiment changé.
            Activated += (sender, e) =>
            {
                if (!_pageReady) return;
                string before = OverlayJson();
                if (_app.ReloadOverlayFromIni() && OverlayJson() != before) PostOverlay();
            };
        }

        /// <summary>Page demandée (menu de l'icône) : affichée dès que l'interface est prête.</summary>
        public void Navigate(string page)
        {
            _page = page;
            if (_pageReady) Post(Json(w => { w.WriteString("type", "go"); w.WriteString("page", page); }));
        }

        /// <summary>L'aperçu de la fenêtre capturée est à l'écran (la capture doit alors tourner).</summary>
        public bool WindowPreviewShown
        {
            get { return _pageReady && Visible && WindowState != FormWindowState.Minimized && _page == "window"; }
        }

        // --- WebView2 --------------------------------------------------------------------------------

        private async void InitWebView()
        {
            string uiFolder = Path.Combine(AppContext.BaseDirectory, "ui");
            try
            {
                if (!File.Exists(Path.Combine(uiFolder, "index.html")))
                    throw new FileNotFoundException(Loc.T("Dossier de l'interface introuvable : ", "Interface folder not found: ") + uiFolder);
                // Lève WebView2RuntimeNotFoundException si le moteur n'est pas installé.
                CoreWebView2Environment.GetAvailableBrowserVersionString();
                string data = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "XRPerfOverlay", "WebView2");
                CoreWebView2Environment environment = await CoreWebView2Environment.CreateAsync(null, data);
                if (IsDisposed) return;
                _web = new WebView2 { Dock = DockStyle.Fill, DefaultBackgroundColor = BackColor };
                Controls.Add(_web);
                await _web.EnsureCoreWebView2Async(environment);
                if (IsDisposed) return;
                CoreWebView2 core = _web.CoreWebView2;
                core.Settings.AreDevToolsEnabled = Debugger.IsAttached;
                core.Settings.AreDefaultContextMenusEnabled = false;
                core.Settings.IsStatusBarEnabled = false;
                core.Settings.IsZoomControlEnabled = false;
                core.Settings.AreBrowserAcceleratorKeysEnabled = false;
                core.Settings.IsGeneralAutofillEnabled = false;
                core.Settings.IsPasswordAutosaveEnabled = false;
                core.SetVirtualHostNameToFolderMapping(HostName, uiFolder, CoreWebView2HostResourceAccessKind.DenyCors);
                core.WebMessageReceived += (sender, e) => OnMessage(e.WebMessageAsJson);
                // La page ne quitte jamais l'interface ; un lien externe s'ouvre dans le navigateur par défaut.
                core.NavigationStarting += (sender, e) =>
                {
                    if (!e.Uri.StartsWith("https://" + HostName + "/", StringComparison.OrdinalIgnoreCase)) e.Cancel = true;
                };
                core.NewWindowRequested += (sender, e) => e.Handled = true;
                core.ProcessFailed += (sender, e) =>
                {
                    _pageReady = false;
                    CoreWebView2ProcessFailedKind kind = e.ProcessFailedKind;
                    if (kind == CoreWebView2ProcessFailedKind.BrowserProcessExited)
                    {
                        // Ce moteur ne peut plus servir : un nouveau est créé (après ce gestionnaire).
                        BeginInvoke(new Action(RestartWebView));
                    }
                    else if (kind == CoreWebView2ProcessFailedKind.RenderProcessExited ||
                             kind == CoreWebView2ProcessFailedKind.RenderProcessUnresponsive)
                    {
                        try { core.Reload(); } catch (Exception) { }
                    }
                    // Autres processus (GPU, utilitaires) : WebView2 les relance seul.
                };
                core.Navigate("https://" + HostName + "/index.html");
            }
            catch (WebView2RuntimeNotFoundException)
            {
                if (IsDisposed) return;
                ShowProblem(Loc.T("Le moteur WebView2 de Microsoft, qui affiche cette fenêtre, n'est pas installé sur ce PC. " +
                                  "Il est fourni avec Windows 11 et avec les mises à jour de Windows 10 ; vous pouvez aussi l'installer vous-même.",
                                  "Microsoft's WebView2 engine, which displays this window, is not installed on this PC. " +
                                  "It comes with Windows 11 and with Windows 10 updates; you can also install it yourself."), true);
            }
            catch (Exception ex)
            {
                if (IsDisposed) return;
                ShowProblem(Loc.T("L'interface n'a pas pu s'ouvrir : ", "The interface could not open: ") + ex.Message, false);
            }
        }

        private void RestartWebView()
        {
            if (IsDisposed) return;
            if (_web != null)
            {
                Controls.Remove(_web);
                try { _web.Dispose(); } catch (Exception) { }
                _web = null;
            }
            InitWebView();
        }

        // Sans WebView2 : un message et, si utile, le lien pour l'installer. Le reste du programme fonctionne.
        private void ShowProblem(string text, bool offerDownload)
        {
            if (_web != null) { Controls.Remove(_web); _web.Dispose(); _web = null; }
            var panel = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, Padding = new Padding(32), BackColor = SystemColors.Window };
            panel.Controls.Add(new Label { Text = text, AutoSize = true, MaximumSize = new Size(620, 0), Font = new Font(Font.FontFamily, 11f), Margin = new Padding(0, 0, 0, 16) });
            if (offerDownload)
            {
                var link = new LinkLabel { Text = Loc.T("Télécharger WebView2 (site de Microsoft)", "Download WebView2 (Microsoft website)"), AutoSize = true, Font = new Font(Font.FontFamily, 11f) };
                link.LinkClicked += (sender, e) => TrayApp.OpenUrl("https://developer.microsoft.com/microsoft-edge/webview2/");
                panel.Controls.Add(link);
            }
            panel.Controls.Add(new Label
            {
                Text = Loc.T("Le panneau, la montre, les rapports et le menu de l'icône près de l'horloge fonctionnent sans cette fenêtre.",
                             "The panel, the watch, the reports and the tray icon menu work without this window."),
                AutoSize = true, MaximumSize = new Size(620, 0), ForeColor = SystemColors.GrayText, Margin = new Padding(0, 16, 0, 0),
            });
            Controls.Add(panel);
            BackColor = SystemColors.Window;
        }

        private void Post(string json)
        {
            if (IsDisposed || _web == null || _web.IsDisposed) return;
            try
            {
                CoreWebView2 core = _web.CoreWebView2;
                if (core != null) core.PostWebMessageAsJson(json);
            }
            catch (Exception)
            {
                // Page en cours de rechargement : le message est perdu, la page redemande tout à "ready".
            }
        }

        private static string Json(Action<Utf8JsonWriter> write)
        {
            using (var stream = new MemoryStream())
            {
                using (var w = new Utf8JsonWriter(stream))
                {
                    w.WriteStartObject();
                    write(w);
                    w.WriteEndObject();
                }
                return Encoding.UTF8.GetString(stream.ToArray());
            }
        }

        private static void Number(Utf8JsonWriter w, string name, float value)
        {
            if (float.IsNaN(value) || float.IsInfinity(value)) w.WriteNull(name);
            else w.WriteNumber(name, Math.Round(value, 2));
        }

        // --- Messages de la page --------------------------------------------------------------------

        private void OnMessage(string json)
        {
            try
            {
                using (JsonDocument document = JsonDocument.Parse(json))
                {
                    JsonElement m = document.RootElement;
                    string type = Str(m, "type");
                    switch (type)
                    {
                        case "ready": SendInit(); break;
                        case "page":
                            _page = Str(m, "name") ?? "dashboard";
                            _app.UpdateCapture();
                            if (_page == "window") SendWindows();
                            FastTick();
                            SlowTick();
                            break;
                        case "set": OnSet(Str(m, "scope"), Str(m, "key"), m.TryGetProperty("value", out JsonElement value) ? value : default(JsonElement)); break;
                        case "resetDefaults": _app.ResetOverlayDefaults(); PostOverlay(); break;
                        case "setLayer": _app.SetLayerEnabled(m.TryGetProperty("on", out JsonElement layerOn) && layerOn.ValueKind == JsonValueKind.True); break;
                        case "fullscreen": SetFullscreen(m.TryGetProperty("on", out JsonElement on) && on.ValueKind == JsonValueKind.True); break;
                        case "chrome": ApplyChrome(Str(m, "background"), m.TryGetProperty("light", out JsonElement light) && light.ValueKind == JsonValueKind.True); break;
                        case "openUrl":
                            string url = Str(m, "url");
                            if (url == "https://pawnio.eu/") TrayApp.OpenUrl(url);
                            break;
                        case "listReports": _reports.Refresh(false); SendReports(); break;
                        case "openReport": SendReport(Ids(m)); break;
                        case "setReadMode":
                            _app.Desktop.ReportsSimple = Str(m, "mode") != "detailed";
                            _app.Desktop.Save();
                            break;
                        case "openNotepad":
                            string notepadError = ReportStore.OpenInNotepad(Ids(m).Select(_reports.Find).Where(r => r != null));
                            if (notepadError != null) Later(() => MessageBox.Show(this, notepadError, Text, MessageBoxButtons.OK, MessageBoxIcon.Warning));
                            break;
                        case "openFolder":
                            string folderError = _reports.OpenFolder(_reports.Find(Str(m, "id")));
                            if (folderError != null) Later(() => MessageBox.Show(this, folderError, Text, MessageBoxButtons.OK, MessageBoxIcon.Warning));
                            break;
                        case "deleteReports":
                            List<string> toDelete = Ids(m);
                            string question = Str(m, "question");
                            Later(() => DeleteReports(toDelete, question));
                            break;
                        case "listWindows": SendWindows(); break;
                        case "chooseWindow": ChooseWindow(Str(m, "id")); break;
                    }
                }
            }
            catch (Exception)
            {
                // Message mal formé : ignoré.
            }
        }

        private static string Str(JsonElement e, string name)
        {
            return e.ValueKind == JsonValueKind.Object && e.TryGetProperty(name, out JsonElement v) && v.ValueKind == JsonValueKind.String ? v.GetString() : null;
        }

        private static List<string> Ids(JsonElement m)
        {
            var ids = new List<string>();
            if (m.TryGetProperty("ids", out JsonElement array) && array.ValueKind == JsonValueKind.Array)
                foreach (JsonElement id in array.EnumerateArray())
                    if (id.ValueKind == JsonValueKind.String) ids.Add(id.GetString());
            return ids;
        }

        private void SendInit()
        {
            _pageReady = true;
            _reports.Refresh(true);
            Post(Json(w =>
            {
                w.WriteString("type", "init");
                w.WriteString("version", AppVersion.Text);
                w.WriteString("layerVersion", _app.LayerVersion);
                w.WriteString("lang", Loc.Code);
                w.WriteString("page", _page);
                w.WriteString("readMode", _app.Desktop.ReportsSimple ? "simple" : "detailed");
                w.WritePropertyName("overlay");
                SettingsJson.WriteOverlay(w, _app.Overlay);
                w.WritePropertyName("desktop");
                SettingsJson.WriteDesktop(w, _app.Desktop);
                if (_app.Unavailable != null) w.WriteString("unavailable", _app.Unavailable);
                w.WriteBoolean("captureAvailable", _app.Capture != null && _app.Capture.Available);
                w.WriteBoolean("previewAvailable", _app.PreviewAvailable);
                w.WriteBoolean("pawnio", LibreHardwareMonitor.PawnIo.PawnIo.IsInstalled);
                w.WriteBoolean("fullscreen", _fullscreen);
                WriteLayer(w);
                IList<string> folders = _app.ReportFolders();
                w.WriteString("reportsFolder", folders.Count > 0 ? folders[0] : "");
            }));
            Post(ReportJson.List(_reports.Reports, _reports.Folders, _reports.PreviousOf, Loc.English));
            _lastStatus = "";
            SlowTick();
            FastTick();
            UpdateTimers();
        }

        // --- Layer OpenXR (activée ou non dans le registre) -------------------------------------------

        private string _lastLayer = "";

        private void WriteLayer(Utf8JsonWriter w)
        {
            LayerRegistry.State state = _app.LayerState;
            _lastLayer = state.ToString() + _app.LayerBusy;
            w.WriteStartObject("layer");
            w.WriteString("state", state == LayerRegistry.State.Enabled ? "enabled" : state == LayerRegistry.State.Disabled ? "disabled" : "missing");
            w.WriteBoolean("busy", _app.LayerBusy);
            w.WriteBoolean("elevated", LayerRegistry.IsElevated());
            w.WriteEndObject();
        }

        /// <summary>État de la layer à jour dans la page (après un changement, ou s'il a changé ailleurs).</summary>
        public void PostLayer()
        {
            if (_pageReady) Post(Json(w => { w.WriteString("type", "layer"); WriteLayer(w); }));
        }

        /// <summary>Court message en bas de la page.</summary>
        public void PostToast(string text)
        {
            if (_pageReady) Post(Json(w => { w.WriteString("type", "toast"); w.WriteString("text", text); }));
        }

        private void PostOverlay()
        {
            Post(Json(w => { w.WriteString("type", "overlay"); w.WritePropertyName("overlay"); SettingsJson.WriteOverlay(w, _app.Overlay); }));
        }

        private string OverlayJson()
        {
            return Json(w => { w.WritePropertyName("o"); SettingsJson.WriteOverlay(w, _app.Overlay); });
        }

        /// <summary>Les réglages du bureau ont changé ailleurs (menu de l'icône, fenêtre miroir fermée ou déplacée).</summary>
        public void PostDesktop()
        {
            Post(Json(w => { w.WriteString("type", "desktop"); w.WritePropertyName("desktop"); SettingsJson.WriteDesktop(w, _app.Desktop); }));
        }

        /// <summary>Les réglages du casque ont changé ailleurs (relecture du .ini).</summary>
        public void OverlayReloaded()
        {
            PostOverlay();
        }

        // Un réglage changé dans la page.
        private void OnSet(string scope, string key, JsonElement value)
        {
            if (key == null) return;
            if (scope == "overlay")
            {
                if (_app.Unavailable != null || !SettingsJson.ApplyOverlay(_app.Overlay, key, value)) return;
                _app.OverlayChanged();
                FastTick(); // aperçu redessiné tout de suite
                return;
            }
            if (scope != "desktop") return;
            Settings d = _app.Desktop;
            string before = d.Language;
            SettingsJson.Change change = SettingsJson.ApplyDesktop(d, key, value);
            switch (change)
            {
                case SettingsJson.Change.None: return;
                case SettingsJson.Change.Mirror: _app.DesktopChanged(); break;
                case SettingsJson.Change.Capture: _app.CaptureChanged(); FastTick(); break;
                case SettingsJson.Change.Interface: _app.SaveDesktopSoon(); break;
                case SettingsJson.Change.Language:
                    d.Save();
                    if (d.Language != before)
                    {
                        _app.LanguageChanged();
                        Post(Json(w => { w.WriteString("type", "lang"); w.WriteString("lang", Loc.Code); }));
                        _lastStatus = "";
                    }
                    break;
            }
        }

        // --- Rapports ---------------------------------------------------------------------------------

        private void SendReports()
        {
            Post(ReportJson.List(_reports.Reports, _reports.Folders, _reports.PreviousOf, Loc.English));
        }

        // Le rapport choisi, comparé au second choisi ou, à défaut, à la session précédente du même jeu.
        private void SendReport(List<string> ids)
        {
            SessionReport report = ids.Count > 0 ? _reports.Find(ids[0]) : null;
            if (report == null) return;
            SessionReport previous = _reports.PreviousOf(report);
            SessionReport chosen = ids.Count > 1 ? _reports.Find(ids[1]) : null;
            SessionReport other = chosen ?? previous;
            Post(ReportJson.Message(report, previous, other, other != null ? _reports.PreviousOf(other) : null, chosen != null, Loc.English));
        }

        // Une boîte de dialogue ne s'ouvre jamais dans un gestionnaire d'événement de WebView2 (boucle de
        // messages imbriquée, non prise en charge) : elle attend que le gestionnaire soit terminé.
        private void Later(Action action)
        {
            if (IsDisposed || !IsHandleCreated) return;
            BeginInvoke(new Action(() => { if (!IsDisposed) action(); }));
        }

        private void DeleteReports(List<string> ids, string question)
        {
            List<SessionReport> selected = ids.Select(_reports.Find).Where(r => r != null).ToList();
            if (selected.Count == 0) return;
            string text = (question ?? Loc.T("Envoyer à la corbeille ?", "Move to the Recycle Bin?")) +
                          (selected.Count == 1 ? "\n\n" + Path.GetFileName(selected[0].Path) : "");
            if (MessageBox.Show(this, text, Text, MessageBoxButtons.YesNo, MessageBoxIcon.Question, MessageBoxDefaultButton.Button2) != DialogResult.Yes) return;
            List<string> failures = _reports.Delete(selected);
            SendReports();
            if (failures.Count > 0)
                MessageBox.Show(this, Loc.T("Certains rapports n'ont pas pu être supprimés :", "Some reports could not be deleted:") + "\n\n" + string.Join("\n", failures),
                    Text, MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }

        // --- Fenêtre capturée ---------------------------------------------------------------------------

        // Fenêtres ouvertes ; la fenêtre choisie reste en tête de liste, même fermée.
        private void SendWindows()
        {
            if (_app.Capture == null) return;
            Settings d = _app.Desktop;
            List<DesktopWindow> windows = WindowCapture.ListWindows();
            IntPtr current = WindowCapture.Find(d);
            Post(Json(w =>
            {
                w.WriteString("type", "windows");
                w.WriteStartArray("windows");
                if (current == IntPtr.Zero && (d.CaptureTitle.Length > 0 || d.CaptureProcess.Length > 0))
                {
                    w.WriteStartObject();
                    w.WriteString("id", "");
                    w.WriteString("title", d.CaptureTitle);
                    w.WriteString("process", d.CaptureProcess);
                    w.WriteBoolean("closed", true);
                    w.WriteBoolean("selected", true);
                    w.WriteEndObject();
                }
                foreach (DesktopWindow window in windows)
                {
                    w.WriteStartObject();
                    w.WriteString("id", window.Handle.ToInt64().ToString(System.Globalization.CultureInfo.InvariantCulture));
                    w.WriteString("title", window.Title);
                    w.WriteString("process", window.Process);
                    w.WriteBoolean("selected", window.Handle == current);
                    w.WriteEndObject();
                }
                w.WriteEndArray();
            }));
        }

        private void ChooseWindow(string id)
        {
            if (_app.Capture == null || !long.TryParse(id, out long value)) return;
            DesktopWindow chosen = WindowCapture.ListWindows().FirstOrDefault(x => x.Handle.ToInt64() == value);
            if (chosen == null) { SendWindows(); return; }
            _app.Desktop.CaptureTitle = chosen.Title;
            _app.Desktop.CaptureProcess = chosen.Process;
            _app.Capture.Retarget(chosen.Handle);
            _app.Capture.ResetPreview();
            _app.CaptureChanged();
            FastTick();
        }

        // --- Images et valeurs en direct ---------------------------------------------------------------

        private bool Live
        {
            get { return _pageReady && Visible && WindowState != FormWindowState.Minimized; }
        }

        private void UpdateTimers()
        {
            bool live = Live;
            _fast.Enabled = live;
            _slow.Enabled = live;
            if (!live) _app.PanelReaderIdle();
        }

        protected override void OnVisibleChanged(EventArgs e)
        {
            base.OnVisibleChanged(e);
            UpdateTimers();
        }

        protected override void OnResize(EventArgs e)
        {
            base.OnResize(e);
            if (IsHandleCreated) UpdateTimers();
        }

        // Quatre fois par seconde : image du panneau (jeu en cours ou aperçu), aperçu de la fenêtre capturée.
        private void FastTick()
        {
            if (!Live) return;
            // Tant que l'interface est visible, la layer publie l'image du panneau : elle dit aussi si un jeu tourne.
            PanelShare share = _app.PanelReader();
            int contentHeight = 0;
            if (share != null)
            {
                bool fresh = share.TryRead(_panelPixels, ref _panelSequence, out int state, out int height);
                _gameState = state;
                if (fresh) contentHeight = height;
                if (state == PanelShare.StateNone) _panelSequence = -1;
            }
            if (Array.IndexOf(PanelPages, _page) >= 0)
            {
                if (_gameState == PanelShare.StateLive)
                {
                    // Image du jeu, déjà en BGRA.
                    if (contentHeight > 0) PostImage("panelImage", Png(_panelPixels, PanelShare.PanelWidth, contentHeight, false), true);
                }
                else if (_gameState == PanelShare.StateNone)
                {
                    int height = _app.RenderPreview(_panelPixels);
                    if (height > 0) PostImage("panelImage", Png(_panelPixels, PanelShare.PanelWidth, height, true), false);
                }
            }
            if (_page == "window" && _app.Capture != null)
            {
                Bitmap image = _app.Capture.TakePreview();
                string status = CaptureStatus();
                if (image != null)
                {
                    using (image)
                    using (var stream = new MemoryStream())
                    {
                        image.Save(stream, ImageFormat.Png);
                        string png = Convert.ToBase64String(stream.ToArray());
                        Post(Json(w => { w.WriteString("type", "windowImage"); w.WriteString("png", png); w.WriteString("status", status); }));
                    }
                }
            }
        }

        private string CaptureStatus()
        {
            if (_app.Capture == null) return "";
            string status = _app.Capture.StatusText();
            return status;
        }

        private void PostImage(string type, string png, bool live)
        {
            if (png == null) return;
            Post(Json(w => { w.WriteString("type", type); w.WriteString("png", png); w.WriteBoolean("live", live); }));
        }

        // Une fois par seconde : capteurs, état du jeu et de la capture, montre, nouveaux rapports.
        private void SlowTick()
        {
            if (!Live) return;
            _slowTicks++;
            Readings r = _app.Latest;
            if (r != null)
            {
                Post(Json(w =>
                {
                    w.WriteString("type", "sensors");
                    w.WriteStartObject("sensors");
                    w.WriteString("cpuName", r.CpuName);
                    w.WriteString("gpuName", r.GpuName);
                    Number(w, "cpuLoad", r.CpuLoad); Number(w, "cpuTemp", r.CpuTemp); Number(w, "cpuMHz", r.CpuMHz);
                    Number(w, "gpuLoad", r.GpuLoad); Number(w, "gpuTemp", r.GpuTemp); Number(w, "gpuMHz", r.GpuMHz);
                    Number(w, "vramUsed", r.VramUsedMB); Number(w, "vramTotal", r.VramTotalMB);
                    w.WriteEndObject();
                }));
            }
            string game = _gameState == PanelShare.StateLive ? "live" : _gameState == PanelShare.StatePaused ? "paused" : "none";
            string sensors = _app.SensorError != null ? "error" : r != null ? "ok" : "wait";
            string capture = CaptureStatus();
            string status = game + "|" + sensors + "|" + capture;
            // Layer activée ou désactivée ailleurs (menu de l'icône, installateur) : la page suit.
            if (_slowTicks % 2 == 0 && _app.LayerState.ToString() + _app.LayerBusy != _lastLayer) PostLayer();
            if (status != _lastStatus)
            {
                _lastStatus = status;
                Post(Json(w => { w.WriteString("type", "status"); w.WriteString("game", game); w.WriteString("sensors", sensors); w.WriteString("capture", capture); }));
            }
            if (_page == "watch")
            {
                int side = _app.RenderWatch(_watchPixels);
                if (side > 0) PostImage("watchImage", Png(_watchPixels, side, side, true), false);
            }
            // Nouveaux rapports (fin d'une session) : toutes les trois secondes, sur les pages qui les montrent.
            if ((_page == "reports" || _page == "dashboard") && _slowTicks % 3 == 0 && _reports.Refresh(false)) SendReports();
        }

        /// <summary>Image PNG (base64) d'un tampon de pixels à alpha prémultiplié, en RGBA (layer) ou déjà en BGRA.</summary>
        private static string Png(byte[] pixels, int width, int height, bool rgba)
        {
            if (width <= 0 || height <= 0 || pixels.Length < width * height * 4) return null;
            using (var bitmap = new Bitmap(width, height, PixelFormat.Format32bppPArgb))
            {
                BitmapData data = bitmap.LockBits(new Rectangle(0, 0, width, height), ImageLockMode.WriteOnly, PixelFormat.Format32bppPArgb);
                try
                {
                    var row = new byte[width * 4];
                    for (int y = 0; y < height; y++)
                    {
                        Buffer.BlockCopy(pixels, y * width * 4, row, 0, row.Length);
                        if (rgba)
                        {
                            for (int x = 0; x < row.Length; x += 4)
                            {
                                byte red = row[x];
                                row[x] = row[x + 2];
                                row[x + 2] = red;
                            }
                        }
                        Marshal.Copy(row, 0, IntPtr.Add(data.Scan0, y * data.Stride), row.Length);
                    }
                }
                finally
                {
                    bitmap.UnlockBits(data);
                }
                using (var stream = new MemoryStream())
                {
                    bitmap.Save(stream, ImageFormat.Png);
                    return Convert.ToBase64String(stream.ToArray());
                }
            }
        }

        // --- Fenêtre ------------------------------------------------------------------------------------

        [DllImport("dwmapi.dll")]
        private static extern int DwmSetWindowAttribute(IntPtr window, int attribute, ref int value, int size);

        // Barre de titre aux couleurs de l'interface : sombre ou claire (Windows 10 et 11), teinte du fond (Windows 11).
        private void ApplyChrome(string background, bool light)
        {
            if (Settings.IsColor(background))
            {
                int value = int.Parse(background.Substring(1), System.Globalization.NumberStyles.HexNumber, System.Globalization.CultureInfo.InvariantCulture);
                Color color = Color.FromArgb((value >> 16) & 255, (value >> 8) & 255, value & 255);
                if (_web != null) BackColor = color;
                try
                {
                    int dark = light ? 0 : 1;
                    DwmSetWindowAttribute(Handle, 20, ref dark, sizeof(int));        // DWMWA_USE_IMMERSIVE_DARK_MODE
                    int caption = color.R | (color.G << 8) | (color.B << 16);
                    DwmSetWindowAttribute(Handle, 35, ref caption, sizeof(int));     // DWMWA_CAPTION_COLOR (Windows 11)
                }
                catch (Exception)
                {
                    // Windows trop ancien : barre de titre ordinaire.
                }
            }
        }

        private void SetFullscreen(bool on)
        {
            if (on == _fullscreen) { Post(Json(w => { w.WriteString("type", "fullscreen"); w.WriteBoolean("on", _fullscreen); })); return; }
            if (on)
            {
                _restoreState = WindowState;
                if (WindowState == FormWindowState.Maximized) WindowState = FormWindowState.Normal;
                _restoreBounds = Bounds;
                FormBorderStyle = FormBorderStyle.None;
                Bounds = Screen.FromControl(this).Bounds;
            }
            else
            {
                FormBorderStyle = FormBorderStyle.Sizable;
                Bounds = _restoreBounds;
                WindowState = _restoreState;
            }
            _fullscreen = on;
            Post(Json(w => { w.WriteString("type", "fullscreen"); w.WriteBoolean("on", on); }));
        }

        protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
        {
            // F11 et Échap arrivent ici quand la page n'a pas le clavier (pendant son chargement).
            if (keyData == Keys.F11) { SetFullscreen(!_fullscreen); return true; }
            if (keyData == Keys.Escape && _fullscreen) { SetFullscreen(false); return true; }
            return base.ProcessCmdKey(ref msg, keyData);
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            // Taille et position mémorisées (celles d'avant le plein écran).
            Settings s = _app.Desktop;
            Rectangle bounds = _fullscreen ? _restoreBounds : (WindowState == FormWindowState.Normal ? Bounds : RestoreBounds);
            bool maximized = _fullscreen ? _restoreState == FormWindowState.Maximized : WindowState == FormWindowState.Maximized;
            if (bounds.Width > 0 && bounds.Height > 0)
            {
                s.WinX = bounds.X; s.WinY = bounds.Y; s.WinWidth = bounds.Width; s.WinHeight = bounds.Height;
            }
            s.WinMaximized = maximized;
            s.Save();
            _pageReady = false;
            _fast.Stop();
            _slow.Stop();
            base.OnFormClosing(e);
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _fast.Dispose();
                _slow.Dispose();
                if (_web != null) _web.Dispose();
            }
            base.Dispose(disposing);
        }
    }
}
