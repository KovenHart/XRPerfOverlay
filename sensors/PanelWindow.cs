// Fenêtre du bureau qui montre le même panneau que dans le casque.
//
// But : enregistrer ou diffuser avec OBS sans passer par un miroir OpenXR. On capture le miroir du
// jeu comme d'habitude, et on ajoute cette fenêtre ("XR Perf Overlay") comme seconde source.
// L'image vient de la layer, par une mémoire partagée qu'elle ne remplit que si cette fenêtre est
// affichée : fenêtre fermée, le jeu ne fait aucun travail supplémentaire.

using System;
using System.ComponentModel;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

namespace XRPerfSensors
{
    /// <summary>
    /// Mémoire partagée "Local\XRPerfOverlay.Panel".
    /// La disposition doit rester identique à PanelShareHeader dans layer/src/shared_block.h.
    /// </summary>
    internal sealed class PanelShare : IDisposable
    {
        public const int PanelWidth = 640;
        public const int PanelHeight = 320;  // hauteur de l'image partagée (panneau avec ses deux lignes du bas)
        public const int BodyHeight = 256;   // hauteur du panneau sans les lignes du bas
        public const int PixelBytes = PanelWidth * PanelHeight * 4;

        private const string Name = @"Local\XRPerfOverlay.Panel";
        private const int HeaderSize = 32;
        private const int Size = HeaderSize + PixelBytes;
        private const uint Magic = 0x50505258; // "XRPP"
        private const uint Version = 3;

        // État de la source d'images, d'après l'horodatage que la layer écrit à chaque copie.
        public const int StateNone = 0;   // aucun jeu : rien n'a été publié, ou la session s'est terminée
        public const int StateLive = 1;   // un jeu publie des images
        public const int StatePaused = 2; // un jeu a publié il y a peu mais plus rien n'arrive (chargement, pause)
        private const long LiveMs = 3000;
        private const long PausedMs = 30000;

        // Offsets dans l'en-tête.
        private const int OffsetSequence = 8;
        private const int OffsetContentHeight = 12;
        private const int OffsetReaderTick = 16;
        private const int OffsetWriterTick = 24;

        private const uint PAGE_READWRITE = 0x04;
        private const uint FILE_MAP_WRITE = 0x0002;
        private const uint SDDL_REVISION_1 = 1;
        private static readonly IntPtr InvalidHandle = new IntPtr(-1);

        // Ici c'est le jeu (non administrateur) qui écrit : lecture et écriture pour les utilisateurs
        // authentifiés (AU) et les applications en bac à sable (AC).
        private const string Sddl = "D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;AU)(A;;GRGW;;;AC)";

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

        public PanelShare()
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
                _mapping = CreateFileMappingW(InvalidHandle, ref attributes, PAGE_READWRITE, 0, (uint)Size, Name);
                if (_mapping == IntPtr.Zero)
                    throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            finally
            {
                LocalFree(descriptor);
            }

            _view = MapViewOfFile(_mapping, FILE_MAP_WRITE, 0, 0, new UIntPtr((uint)Size));
            if (_view == IntPtr.Zero)
            {
                int error = Marshal.GetLastWin32Error();
                CloseHandle(_mapping);
                _mapping = IntPtr.Zero;
                throw new Win32Exception(error);
            }

            Marshal.WriteInt64(_view, OffsetReaderTick, 0);
            Marshal.WriteInt32(_view, 0, unchecked((int)Magic));
            Marshal.WriteInt32(_view, 4, unchecked((int)Version));
        }

        /// <summary>Signale à la layer que la fenêtre est affichée (à appeler plusieurs fois par seconde).</summary>
        public void Heartbeat()
        {
            if (_view != IntPtr.Zero) Marshal.WriteInt64(_view, OffsetReaderTick, Environment.TickCount64);
        }

        /// <summary>Signale à la layer qu'elle peut arrêter de publier l'image.</summary>
        public void StopWatching()
        {
            if (_view != IntPtr.Zero) Marshal.WriteInt64(_view, OffsetReaderTick, 0);
        }

        /// <summary>
        /// Copie la dernière image dans <paramref name="buffer"/> (octets BGRA, alpha prémultiplié) si
        /// elle est nouvelle. <paramref name="state"/> : StateNone, StateLive ou StatePaused.
        /// <paramref name="contentHeight"/> : nombre de lignes dessinées, en partant du haut (0 si rien n'a été lu).
        /// </summary>
        public bool TryRead(byte[] buffer, ref int lastSequence, out int state, out int contentHeight)
        {
            state = StateNone;
            contentHeight = 0;
            if (_view == IntPtr.Zero || buffer == null || buffer.Length < PixelBytes) return false;

            // La layer remet l'horodatage à zéro quand la session du jeu se termine. Un horodatage
            // ancien mais non nul vient d'un jeu en plein chargement (ou fermé brutalement).
            long writer = Marshal.ReadInt64(_view, OffsetWriterTick);
            long now = Environment.TickCount64;
            if (writer == 0 || now < writer || now - writer > PausedMs) return false;
            if (now - writer > LiveMs)
            {
                state = StatePaused;
                return false;
            }
            state = StateLive;

            // La layer passe la séquence à une valeur impaire pendant qu'elle copie l'image.
            for (int attempt = 0; attempt < 3; attempt++)
            {
                int before = Marshal.ReadInt32(_view, OffsetSequence);
                if ((before & 1) != 0)
                {
                    Thread.Sleep(1);
                    continue;
                }
                if (before == lastSequence) return false; // rien de nouveau

                Marshal.Copy(IntPtr.Add(_view, HeaderSize), buffer, 0, PixelBytes);
                int height = Marshal.ReadInt32(_view, OffsetContentHeight);
                Thread.MemoryBarrier();
                if (Marshal.ReadInt32(_view, OffsetSequence) != before) continue; // modifiée pendant la copie
                contentHeight = (height >= 1 && height <= PanelHeight) ? height : PanelHeight;

                SwapRedBlue(buffer);
                lastSequence = before;
                return true;
            }
            return false;
        }

        /// <summary>La layer écrit en RGBA, GDI+ attend du BGRA : on échange rouge et bleu.</summary>
        public static void SwapRedBlue(byte[] buffer)
        {
            for (int i = 0; i + 3 < buffer.Length && i < PixelBytes; i += 4)
            {
                byte red = buffer[i];
                buffer[i] = buffer[i + 2];
                buffer[i + 2] = red;
            }
        }

        public void Dispose()
        {
            if (_view != IntPtr.Zero)
            {
                StopWatching();
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

    /// <summary>Fenêtre sans bordure, déplaçable à la souris, qui affiche l'image du panneau.</summary>
    internal sealed class PanelWindow : Form
    {
        private const int WM_NCLBUTTONDOWN = 0xA1;
        private const int HTCAPTION = 2;

        [DllImport("user32.dll")]
        private static extern bool ReleaseCapture();

        [DllImport("user32.dll")]
        private static extern IntPtr SendMessage(IntPtr window, int message, IntPtr wParam, IntPtr lParam);

        private readonly PanelShare _share;
        private readonly Bitmap _bitmap = new Bitmap(PanelShare.PanelWidth, PanelShare.PanelHeight, PixelFormat.Format32bppPArgb);
        private readonly byte[] _pixels = new byte[PanelShare.PixelBytes];
        private readonly System.Windows.Forms.Timer _timer = new System.Windows.Forms.Timer { Interval = 100 };
        private int _lastSequence = -1;
        private bool _hasImage;
        private bool _stale = true;
        private int _contentHeight = PanelShare.PanelHeight; // partie de l'image réellement dessinée par la layer
        private int _scalePercent = 100;
        private int _anchor;                       // 0 = libre, 1 à 6 = coin ou bord de l'écran (voir ApplyAnchor)
        private Func<byte[], int> _previewSource;  // dessine l'aperçu hors jeu (RGBA) et retourne sa hauteur, 0 = indisponible
        private bool _previewing;                  // l'image affichée est l'aperçu, pas celle d'un jeu
        private int _ticks;
        private bool _userMoving;                  // l'utilisateur déplace la fenêtre à la souris
        private const int CaptionHeight = 20;      // bandeau "Aperçu" sous l'image
        private const int ScreenMargin = 16;

        public PanelWindow(PanelShare share)
        {
            _share = share;
            Text = "XR Perf Overlay"; // nom sous lequel OBS liste la fenêtre
            FormBorderStyle = FormBorderStyle.None;
            StartPosition = FormStartPosition.Manual;
            ShowInTaskbar = true;
            BackColor = Color.FromArgb(14, 17, 22); // même teinte que le fond du panneau
            DoubleBuffered = true;
            Icon = AppIcon.Load(new Size(32, 32));
            ApplySize();
            _timer.Tick += (s, e) => Poll();
        }

        // S'affiche sans prendre le focus au jeu.
        protected override bool ShowWithoutActivation
        {
            get { return true; }
        }

        /// <summary>Fournit le dessin de l'aperçu affiché quand aucun jeu OpenXR ne tourne.</summary>
        public void SetPreviewSource(Func<byte[], int> source)
        {
            _previewSource = source;
        }

        /// <summary>Taille (en % du panneau), opacité (en %) et emplacement (0 = libre, 1 à 6 = bord de l'écran).</summary>
        public void SetAppearance(int scalePercent, int opacityPercent, int anchor)
        {
            _scalePercent = Math.Min(Settings.MaxScale, Math.Max(Settings.MinScale, scalePercent));
            _anchor = anchor;
            Opacity = Math.Min(100, Math.Max(Settings.MinOpacity, opacityPercent)) / 100.0;
            ApplySize();
        }

        // La fenêtre a exactement les proportions de la partie dessinée du panneau, plus le bandeau de l'aperçu.
        private void ApplySize()
        {
            ClientSize = new Size(PanelShare.PanelWidth * _scalePercent / 100,
                _contentHeight * _scalePercent / 100 + (_previewing ? CaptionHeight : 0));
            ApplyAnchor();
            Invalidate();
        }

        // Place la fenêtre contre un bord de l'écran où elle se trouve (ordre : haut-gauche, haut,
        // haut-droit, bas-droit, bas, bas-gauche). En mode libre, elle reste où l'utilisateur l'a mise.
        private void ApplyAnchor()
        {
            if (_anchor < 1 || _anchor > 6 || _userMoving) return;
            Rectangle area = Screen.FromRectangle(Bounds).WorkingArea;
            int left = area.Left + ScreenMargin, right = area.Right - Width - ScreenMargin, center = area.Left + (area.Width - Width) / 2;
            int top = area.Top + ScreenMargin, bottom = area.Bottom - Height - ScreenMargin;
            int x = _anchor == 1 || _anchor == 6 ? left : (_anchor == 3 || _anchor == 4 ? right : center);
            int y = _anchor <= 3 ? top : bottom;
            Location = new Point(x, y);
        }

        protected override void OnVisibleChanged(EventArgs e)
        {
            base.OnVisibleChanged(e);
            if (Visible)
            {
                _share.Heartbeat();
                _ticks = 0;
                _timer.Start();
                Poll(); // contenu affiché tout de suite, sans attendre le premier battement
            }
            else
            {
                _timer.Stop();
                _share.StopWatching();
            }
        }

        private void Poll()
        {
            _share.Heartbeat();
            bool updated = _share.TryRead(_pixels, ref _lastSequence, out int state, out int contentHeight);
            bool previewing = false;
            // "En attente" seulement s'il n'y a ni jeu ni aperçu ; pendant un chargement, la dernière image reste.
            bool stale = state == PanelShare.StateNone;
            if (state != PanelShare.StateNone)
            {
                _ticks = 0; // l'aperçu reprendra sans délai quand le jeu s'arrêtera
            }
            else
            {
                // Aucun jeu ne publie d'image : aperçu dessiné par la layer, quatre fois par seconde environ.
                previewing = _previewing;
                if (_previewSource != null && _ticks++ % 3 == 0)
                {
                    int height = _previewSource(_pixels);
                    previewing = height > 0;
                    if (previewing)
                    {
                        PanelShare.SwapRedBlue(_pixels);
                        contentHeight = height;
                        updated = true;
                        _lastSequence = -1; // la prochaine image d'un jeu sera prise, quelle que soit sa séquence
                    }
                }
                if (previewing) stale = false;
            }
            if (updated)
            {
                if (contentHeight != _contentHeight || previewing != _previewing)
                {
                    // La hauteur dépend de la présentation choisie ; le bandeau n'existe que pour l'aperçu.
                    _contentHeight = contentHeight;
                    _previewing = previewing;
                    ApplySize();
                }
                // Coins arrondis du panneau : la fenêtre prend la couleur du fond, lue dans l'image (B, G, R, A).
                int corner = (4 * PanelShare.PanelWidth + PanelShare.PanelWidth / 2) * 4;
                if (_pixels[corner + 3] == 255)
                {
                    Color background = Color.FromArgb(_pixels[corner + 2], _pixels[corner + 1], _pixels[corner]);
                    if (background != BackColor) BackColor = background;
                }
                BitmapData data = _bitmap.LockBits(new Rectangle(0, 0, _bitmap.Width, _bitmap.Height), ImageLockMode.WriteOnly, PixelFormat.Format32bppPArgb);
                try
                {
                    Marshal.Copy(_pixels, 0, data.Scan0, PanelShare.PixelBytes);
                }
                finally
                {
                    _bitmap.UnlockBits(data);
                }
                _hasImage = true;
                Invalidate();
            }
            if (stale != _stale)
            {
                _stale = stale;
                Invalidate();
            }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.Clear(BackColor);
            if (_hasImage && !_stale)
            {
                int caption = _previewing ? CaptionHeight : 0;
                g.InterpolationMode = _scalePercent == 100 ? InterpolationMode.NearestNeighbor : InterpolationMode.HighQualityBicubic;
                g.PixelOffsetMode = PixelOffsetMode.Half;
                g.DrawImage(_bitmap, new Rectangle(0, 0, ClientSize.Width, ClientSize.Height - caption),
                    new Rectangle(0, 0, PanelShare.PanelWidth, _contentHeight), GraphicsUnit.Pixel);
                if (_previewing)
                {
                    TextRenderer.DrawText(g, Loc.T("Aperçu avec des FPS d'exemple - aucun jeu VR en cours", "Preview with sample FPS - no VR game running"), Font,
                        new Rectangle(0, ClientSize.Height - caption, ClientSize.Width, caption), Color.FromArgb(128, 136, 150),
                        TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
                }
            }
            else
            {
                TextRenderer.DrawText(g, Loc.T("XR Perf Overlay - en attente d'un jeu VR...", "XR Perf Overlay - waiting for a VR game..."), Font, new Rectangle(Point.Empty, ClientSize),
                    Color.FromArgb(142, 152, 170),
                    TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
            }
        }

        protected override void OnResizeBegin(EventArgs e)
        {
            _userMoving = true; // pendant le déplacement, la fenêtre n'est pas ramenée contre le bord de l'écran
            base.OnResizeBegin(e);
        }

        protected override void OnResizeEnd(EventArgs e)
        {
            _userMoving = false;
            base.OnResizeEnd(e);
        }

        // Déplacement : un appui du bouton gauche n'importe où dans la fenêtre agit comme sur une barre de titre.
        protected override void OnMouseDown(MouseEventArgs e)
        {
            base.OnMouseDown(e);
            if (e.Button == MouseButtons.Left)
            {
                ReleaseCapture();
                SendMessage(Handle, WM_NCLBUTTONDOWN, new IntPtr(HTCAPTION), IntPtr.Zero);
            }
        }

        // Alt+F4 ou fermeture depuis la barre des tâches : on masque seulement, le programme continue.
        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            if (e.CloseReason == CloseReason.UserClosing)
            {
                e.Cancel = true;
                Hide();
            }
            base.OnFormClosing(e);
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _timer.Dispose();
                _bitmap.Dispose();
            }
            base.Dispose(disposing);
        }
    }

    /// <summary>Numéro de version (fichier VERSION à la racine du projet, repris par XRPerfSensors.csproj).</summary>
    internal static class AppVersion
    {
        /// <summary>Version de XRPerfSensors, sous la forme 1.2.3 (« dev » pour une compilation sans le fichier VERSION).</summary>
        public static string Text
        {
            get
            {
                Version v = typeof(AppVersion).Assembly.GetName().Version;
                return v == null || (v.Major == 0 && v.Minor == 0 && v.Build <= 0) ? "dev" : v.ToString(3);
            }
        }

        /// <summary>Version inscrite dans une DLL de la layer (ressource de version), ou chaîne vide.</summary>
        public static string OfFile(string path)
        {
            try
            {
                if (!System.IO.File.Exists(path)) return "";
                string version = System.Diagnostics.FileVersionInfo.GetVersionInfo(path).ProductVersion;
                return version == null ? "" : version.Trim();
            }
            catch (Exception)
            {
                return "";
            }
        }
    }

    /// <summary>Icône du programme, incorporée dans l'exécutable (voir XRPerfSensors.csproj).</summary>
    internal static class AppIcon
    {
        /// <summary>Icône à la taille demandée, ou l'icône par défaut de Windows si la ressource manque.</summary>
        public static Icon Load(Size size)
        {
            try
            {
                var assembly = typeof(AppIcon).Assembly;
                string name = assembly.GetManifestResourceNames()
                    .FirstOrDefault(n => n.EndsWith("XRPerfSensors.ico", StringComparison.OrdinalIgnoreCase));
                if (name != null)
                {
                    using (Stream stream = assembly.GetManifestResourceStream(name))
                    {
                        if (stream != null) return new Icon(stream, size);
                    }
                }
            }
            catch (Exception)
            {
                // Ressource illisible : icône par défaut.
            }
            return SystemIcons.Application;
        }
    }

    /// <summary>Réglages propres à ce PC (fenêtre miroir, capture, interface), mémorisés dans %LOCALAPPDATA%\XRPerfOverlay\XRPerfSensors.ini.</summary>
    internal sealed class Settings
    {
        public const int MinScale = 25, MaxScale = 300; // taille de la fenêtre, en % du panneau
        public const int MinOpacity = 20;                // opacité minimale de la fenêtre, en %

        public bool DesktopPanel;
        public bool TopMost = true;
        public int Scale = 100;
        public int Opacity = 100;
        public int Anchor; // 0 = libre, 1 à 6 = bord de l'écran (ordre de la liste de la fenêtre de réglages)
        public int X = 60;
        public int Y = 60;
        public bool ReportsSimple = true; // fenêtre des rapports : lecture simple (sinon détaillée)

        // Fenêtre capturée pour le casque (voir WindowCapture.cs) : laquelle, et comment la capturer.
        public const int MinCaptureFps = 1, MaxCaptureFps = 15, MaxCrop = 45;
        public string CaptureTitle = "";    // titre de la fenêtre choisie
        public string CaptureProcess = "";  // son programme, par exemple "chrome.exe"
        public int CaptureFps = 5;          // images par seconde
        public int CaptureOpacity = 100;    // %
        public int CropLeft, CropTop, CropRight, CropBottom;  // part retirée de chaque côté, en %

        // Interface (fenêtre principale, ui/app.js).
        public static readonly string[] UiThemes = { "panel", "dark", "light", "contrast", "green", "amber", "violet", "custom" };
        public const int MinUiScale = 85, MaxUiScale = 130;
        public string Language = "auto";       // "auto" (langue de Windows), "fr" ou "en" (Loc.Names)
        public string UiTheme = "panel";       // couleurs de l'interface : comme le panneau, un thème, ou personnalisées
        public string UiBackground = "#0E1116", UiText = "#F0F3F8", UiAccent = "#96BEF0";
        public int UiScale = 100;              // taille du texte, en %
        public bool UiAnimations = true;
        public bool UiStartFullscreen;
        public int WinX = int.MinValue, WinY = int.MinValue, WinWidth = 1280, WinHeight = 820; // fenêtre principale
        public bool WinMaximized;

        private static string FilePath
        {
            get
            {
                return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "XRPerfOverlay", "XRPerfSensors.ini");
            }
        }

        public static Settings Load()
        {
            var settings = new Settings();
            try
            {
                if (!File.Exists(FilePath)) return settings;
                foreach (string line in File.ReadAllLines(FilePath))
                {
                    int equals = line.IndexOf('=');
                    if (equals <= 0) continue;
                    string key = line.Substring(0, equals).Trim();
                    // Textes (titre de fenêtre, programme) : gardés tels quels.
                    if (key == "capture_title") { settings.CaptureTitle = line.Substring(equals + 1).Trim(); continue; }
                    if (key == "capture_process") { settings.CaptureProcess = line.Substring(equals + 1).Trim(); continue; }
                    string text = line.Substring(equals + 1).Trim();
                    if (key == "language") { if (Array.IndexOf(Loc.Names, text) >= 0) settings.Language = text; continue; }
                    if (key == "ui_theme") { if (Array.IndexOf(UiThemes, text) >= 0) settings.UiTheme = text; continue; }
                    if (key == "ui_background") { if (IsColor(text)) settings.UiBackground = text; continue; }
                    if (key == "ui_text") { if (IsColor(text)) settings.UiText = text; continue; }
                    if (key == "ui_accent") { if (IsColor(text)) settings.UiAccent = text; continue; }
                    if (!int.TryParse(line.Substring(equals + 1).Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out int value)) continue;
                    switch (key)
                    {
                        case "desktop_panel": settings.DesktopPanel = value != 0; break;
                        case "topmost": settings.TopMost = value != 0; break;
                        case "scale": if (value >= MinScale && value <= MaxScale) settings.Scale = value; break;
                        case "opacity": if (value >= MinOpacity && value <= 100) settings.Opacity = value; break;
                        case "anchor": if (value >= 0 && value <= 6) settings.Anchor = value; break;
                        case "x": settings.X = value; break;
                        case "y": settings.Y = value; break;
                        case "reports_simple": settings.ReportsSimple = value != 0; break;
                        case "capture_fps": if (value >= MinCaptureFps && value <= MaxCaptureFps) settings.CaptureFps = value; break;
                        case "capture_opacity": if (value >= MinOpacity && value <= 100) settings.CaptureOpacity = value; break;
                        case "crop_left": if (value >= 0 && value <= MaxCrop) settings.CropLeft = value; break;
                        case "crop_top": if (value >= 0 && value <= MaxCrop) settings.CropTop = value; break;
                        case "crop_right": if (value >= 0 && value <= MaxCrop) settings.CropRight = value; break;
                        case "crop_bottom": if (value >= 0 && value <= MaxCrop) settings.CropBottom = value; break;
                        case "ui_scale": if (value >= MinUiScale && value <= MaxUiScale) settings.UiScale = value; break;
                        case "ui_animations": settings.UiAnimations = value != 0; break;
                        case "ui_fullscreen": settings.UiStartFullscreen = value != 0; break;
                        case "win_x": settings.WinX = value; break;
                        case "win_y": settings.WinY = value; break;
                        case "win_width": if (value >= 600 && value <= 20000) settings.WinWidth = value; break;
                        case "win_height": if (value >= 400 && value <= 20000) settings.WinHeight = value; break;
                        case "win_maximized": settings.WinMaximized = value != 0; break;
                    }
                }
            }
            catch (Exception)
            {
                // Fichier illisible : réglages par défaut.
            }
            return settings;
        }

        /// <summary>Couleur « #RRGGBB ».</summary>
        public static bool IsColor(string text)
        {
            return text != null && text.Length == 7 && text[0] == '#' &&
                   int.TryParse(text.Substring(1), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out int unused);
        }

        // Une valeur par ligne : un titre de fenêtre ne doit pas en contenir.
        private static string OneLine(string text)
        {
            return (text ?? "").Replace('\r', ' ').Replace('\n', ' ').Trim();
        }

        public void Save()
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(FilePath));
                File.WriteAllLines(FilePath, new[]
                {
                    "desktop_panel=" + (DesktopPanel ? 1 : 0),
                    "topmost=" + (TopMost ? 1 : 0),
                    "scale=" + Scale.ToString(CultureInfo.InvariantCulture),
                    "opacity=" + Opacity.ToString(CultureInfo.InvariantCulture),
                    "anchor=" + Anchor.ToString(CultureInfo.InvariantCulture),
                    "x=" + X.ToString(CultureInfo.InvariantCulture),
                    "y=" + Y.ToString(CultureInfo.InvariantCulture),
                    "reports_simple=" + (ReportsSimple ? 1 : 0),
                    "capture_title=" + OneLine(CaptureTitle),
                    "capture_process=" + OneLine(CaptureProcess),
                    "capture_fps=" + CaptureFps.ToString(CultureInfo.InvariantCulture),
                    "capture_opacity=" + CaptureOpacity.ToString(CultureInfo.InvariantCulture),
                    "crop_left=" + CropLeft.ToString(CultureInfo.InvariantCulture),
                    "crop_top=" + CropTop.ToString(CultureInfo.InvariantCulture),
                    "crop_right=" + CropRight.ToString(CultureInfo.InvariantCulture),
                    "crop_bottom=" + CropBottom.ToString(CultureInfo.InvariantCulture),
                    "language=" + Language,
                    "ui_theme=" + UiTheme,
                    "ui_background=" + UiBackground,
                    "ui_text=" + UiText,
                    "ui_accent=" + UiAccent,
                    "ui_scale=" + UiScale.ToString(CultureInfo.InvariantCulture),
                    "ui_animations=" + (UiAnimations ? 1 : 0),
                    "ui_fullscreen=" + (UiStartFullscreen ? 1 : 0),
                    "win_x=" + WinX.ToString(CultureInfo.InvariantCulture),
                    "win_y=" + WinY.ToString(CultureInfo.InvariantCulture),
                    "win_width=" + WinWidth.ToString(CultureInfo.InvariantCulture),
                    "win_height=" + WinHeight.ToString(CultureInfo.InvariantCulture),
                    "win_maximized=" + (WinMaximized ? 1 : 0),
                });
            }
            catch (Exception)
            {
                // Dossier inaccessible : les réglages ne sont simplement pas mémorisés.
            }
        }
    }
}
