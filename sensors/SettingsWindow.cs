// Réglages de l'overlay depuis XRPerfSensors : modèle (lu et écrit dans XRPerfOverlay.ini), envoi
// en direct à la layer par une mémoire partagée, aperçu hors jeu, et fenêtre de réglages.

using System;
using System.ComponentModel;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace XRPerfSensors
{
    /// <summary>
    /// Réglages de l'overlay dans le casque. Mêmes clés, mêmes bornes et mêmes valeurs par défaut que
    /// la layer (layer/src/config.cpp) : ce fichier est celui qu'elle lit au lancement d'un jeu.
    /// </summary>
    internal sealed class OverlaySettings
    {
        // Ordre des énumérations Corner, Layout et ThemeId de la layer.
        public static readonly string[] PositionNames = { "top-left", "top", "top-right", "bottom-right", "bottom", "bottom-left" };
        public static readonly string[] LayoutNames = { "classic", "graph", "compact" };
        public static readonly string[] ThemeNames = { "dark", "light", "contrast", "green", "amber", "violet", "custom" };
        public const int CustomTheme = 6;
        // Ordre des énumérations Anchor, HandPlacement et WatchStyle de la layer.
        public static readonly string[] AnchorNames = { "view", "left_hand", "right_hand" };
        public static readonly string[] PlacementNames = { "wrist_back", "wrist_inside", "above" };
        public static readonly string[] WatchStyleNames = { "classic", "digital", "sport" };
        // Ordre de l'énumération WindowAnchor de la layer.
        public static readonly string[] WindowAnchorNames = { "left_hand", "right_hand", "world" };
        public const int WindowWorld = 2;
        // Bornes (mètres), comme layer/src/config.h.
        public const float MinHandAlong = -0.12f, MaxHandAlong = 0.12f, MinHandLift = -0.05f, MaxHandLift = 0.10f;
        public const float MinWatchSize = 0.03f, MaxWatchSize = 0.20f, MinWatchOpacity = 0.2f;
        public const float MinWindowWidth = 0.10f, MaxWindowWidth = 1.50f, MinWindowDistance = 0.30f, MaxWindowDistance = 3.0f;

        public bool Visible = true;
        public int Position = 5; // bas-gauche
        public float Distance = 1.0f;
        public float OffsetX = 0.34f;
        public float OffsetY = 0.26f;
        public float Width = 0.36f;
        public float Opacity = 0.82f;
        public float GlobalOpacity = 1.0f;
        public bool FaceViewer = true;
        public bool Clock = true;
        public bool HeadsetInfo = true;
        public bool PanelSession, PanelBattery;  // ligne « session / batterie » du panneau (option, désactivée par défaut)
        public int Layout;
        public int Theme;
        public Color CustomBackground = Color.FromArgb(14, 17, 22);
        public Color CustomText = Color.FromArgb(240, 243, 248);
        public Color CustomAccent = Color.FromArgb(150, 190, 240);
        public string HeadsetName = "";

        // Panneau accroché à une main (sinon devant les yeux).
        public int Anchor;                  // AnchorNames
        public int HandPlacement = 2;       // PlacementNames : au-dessus de la manette
        public bool GazeOnly;
        public float HandAlong, HandLift;

        // Montre ([watch]).
        public bool WatchEnabled;
        public int WatchAnchor = 1;         // main gauche
        public int WatchPlacement;          // dos du poignet
        public int WatchStyle;              // classique
        public bool WatchSession = true, WatchBattery = true, WatchFps = true;
        public bool WatchGazeOnly = true;
        public float WatchSize = 0.06f;
        public float WatchOpacity = 1f;
        public float WatchAlong, WatchLift;

        // Fenêtre du bureau capturée ([window]) : affichage dans le casque. La fenêtre choisie et la
        // capture elle-même sont réglées dans XRPerfSensors.ini (classe Settings).
        public bool WindowEnabled;
        public int WindowAnchor = WindowWorld;  // WindowAnchorNames : posée dans l'espace
        public int WindowPlacement = 2;         // PlacementNames : au-dessus de la manette
        public bool WindowGazeOnly;
        public float WindowWidth = 0.40f;
        public float WindowAlong, WindowLift;
        public float WindowDistance = 0.80f;    // distance à laquelle Ctrl+Alt+G la pose devant soi

        // Hors section [overlay] : pris en compte par la layer au lancement d'un jeu seulement.
        public bool GpuTiming;             // [gpu] measure : mesure réelle du temps GPU (sinon estimation)
        public bool ReportsEnabled = true; // [summary] enabled : un rapport .txt par session
        public bool ReportHardware = true; // [summary] hardware : modèles du processeur et de la carte graphique dans les rapports
        public string SummaryFolder = "";  // [summary] folder : dossier des rapports (vide = "sessions" à côté de la DLL)
        public bool OpenVR = true;         // [openvr] enabled : XRPerfVR (panneau, montre, rapports dans les jeux SteamVR / OpenVR)

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern uint GetPrivateProfileStringW(string section, string key, string defaultValue, StringBuilder result, uint size, string path);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool WritePrivateProfileStringW(string section, string key, string value, string path);

        private const string Section = "overlay";

        private static string ReadText(string path, string key)
        {
            return ReadText(path, Section, key);
        }

        private static string ReadText(string path, string section, string key)
        {
            var buffer = new StringBuilder(1024);
            GetPrivateProfileStringW(section, key, "", buffer, (uint)buffer.Capacity, path);
            string value = buffer.ToString();
            int comment = value.IndexOf(';'); // commentaire en fin de ligne
            if (comment >= 0) value = value.Substring(0, comment);
            return value.Trim(' ', '\t', '"');
        }

        private static float ReadFloat(string path, string key, float fallback, float min, float max)
        {
            return ReadFloat(path, Section, key, fallback, min, max);
        }

        private static float ReadFloat(string path, string section, string key, float fallback, float min, float max)
        {
            string text = ReadText(path, section, key).Replace(',', '.');
            if (!float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out float value) || float.IsNaN(value)) return fallback;
            return Math.Min(max, Math.Max(min, value));
        }

        private static bool ReadBool(string path, string key, bool fallback)
        {
            return ReadBool(path, Section, key, fallback);
        }

        private static bool ReadBool(string path, string section, string key, bool fallback)
        {
            string text = ReadText(path, section, key);
            if (!int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out int value)) return fallback;
            return value != 0;
        }

        private static int ReadName(string path, string key, string[] names, int fallback)
        {
            return ReadName(path, Section, key, names, fallback);
        }

        private static int ReadName(string path, string section, string key, string[] names, int fallback)
        {
            int index = Array.IndexOf(names, ReadText(path, section, key).ToLowerInvariant());
            return index >= 0 ? index : fallback;
        }

        private static Color ReadColor(string path, string key, Color fallback)
        {
            string text = ReadText(path, key).TrimStart('#');
            if (text.Length != 6 || !int.TryParse(text, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out int value)) return fallback;
            return Color.FromArgb((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
        }

        public static OverlaySettings Load(string path)
        {
            var s = new OverlaySettings();
            s.Visible = ReadBool(path, "visible", s.Visible);
            s.Position = ReadName(path, "position", PositionNames, s.Position);
            s.Distance = ReadFloat(path, "distance", s.Distance, 0.3f, 5f);
            s.OffsetX = ReadFloat(path, "offset_x", s.OffsetX, 0f, 3f);
            s.OffsetY = ReadFloat(path, "offset_y", s.OffsetY, 0f, 3f);
            s.Width = ReadFloat(path, "width", s.Width, 0.10f, 1.50f);
            s.Opacity = ReadFloat(path, "opacity", s.Opacity, 0f, 1f);
            s.GlobalOpacity = ReadFloat(path, "global_opacity", s.GlobalOpacity, 0.20f, 1f);
            s.FaceViewer = ReadBool(path, "face_viewer", s.FaceViewer);
            s.Clock = ReadBool(path, "clock", s.Clock);
            s.HeadsetInfo = ReadBool(path, "headset_info", s.HeadsetInfo);
            s.PanelSession = ReadBool(path, "session", s.PanelSession);
            s.PanelBattery = ReadBool(path, "battery", s.PanelBattery);
            s.Layout = ReadName(path, "layout", LayoutNames, s.Layout);
            s.Theme = ReadName(path, "theme", ThemeNames, s.Theme);
            s.CustomBackground = ReadColor(path, "custom_background", s.CustomBackground);
            s.CustomText = ReadColor(path, "custom_text", s.CustomText);
            s.CustomAccent = ReadColor(path, "custom_accent", s.CustomAccent);
            s.HeadsetName = ReadText(path, "headset_name");
            s.GpuTiming = ReadBool(path, "gpu", "measure", s.GpuTiming);
            s.ReportsEnabled = ReadBool(path, "summary", "enabled", s.ReportsEnabled);
            s.ReportHardware = ReadBool(path, "summary", "hardware", s.ReportHardware);
            s.SummaryFolder = ReadText(path, "summary", "folder");
            s.OpenVR = ReadBool(path, "openvr", "enabled", s.OpenVR);

            s.Anchor = ReadName(path, "anchor", AnchorNames, s.Anchor);
            s.HandPlacement = ReadName(path, "hand_position", PlacementNames, s.HandPlacement);
            s.GazeOnly = ReadBool(path, "gaze_only", s.GazeOnly);
            s.HandAlong = ReadFloat(path, "hand_along", s.HandAlong, MinHandAlong, MaxHandAlong);
            s.HandLift = ReadFloat(path, "hand_lift", s.HandLift, MinHandLift, MaxHandLift);
            s.WatchEnabled = ReadBool(path, "watch", "enabled", s.WatchEnabled);
            s.WatchAnchor = ReadName(path, "watch", "anchor", AnchorNames, s.WatchAnchor);
            s.WatchPlacement = ReadName(path, "watch", "hand_position", PlacementNames, s.WatchPlacement);
            s.WatchStyle = ReadName(path, "watch", "style", WatchStyleNames, s.WatchStyle);
            s.WatchSession = ReadBool(path, "watch", "session", s.WatchSession);
            s.WatchBattery = ReadBool(path, "watch", "battery", s.WatchBattery);
            s.WatchFps = ReadBool(path, "watch", "fps", s.WatchFps);
            s.WatchGazeOnly = ReadBool(path, "watch", "gaze_only", s.WatchGazeOnly);
            s.WatchSize = ReadFloat(path, "watch", "size", s.WatchSize, MinWatchSize, MaxWatchSize);
            s.WatchOpacity = ReadFloat(path, "watch", "opacity", s.WatchOpacity, MinWatchOpacity, 1f);
            s.WatchAlong = ReadFloat(path, "watch", "hand_along", s.WatchAlong, MinHandAlong, MaxHandAlong);
            s.WatchLift = ReadFloat(path, "watch", "hand_lift", s.WatchLift, MinHandLift, MaxHandLift);
            s.WindowEnabled = ReadBool(path, "window", "enabled", s.WindowEnabled);
            s.WindowAnchor = ReadName(path, "window", "anchor", WindowAnchorNames, s.WindowAnchor);
            s.WindowPlacement = ReadName(path, "window", "hand_position", PlacementNames, s.WindowPlacement);
            s.WindowGazeOnly = ReadBool(path, "window", "gaze_only", s.WindowGazeOnly);
            s.WindowWidth = ReadFloat(path, "window", "width", s.WindowWidth, MinWindowWidth, MaxWindowWidth);
            s.WindowAlong = ReadFloat(path, "window", "hand_along", s.WindowAlong, MinHandAlong, MaxHandAlong);
            s.WindowLift = ReadFloat(path, "window", "hand_lift", s.WindowLift, MinHandLift, MaxHandLift);
            s.WindowDistance = ReadFloat(path, "window", "distance", s.WindowDistance, MinWindowDistance, MaxWindowDistance);
            return s;
        }

        public OverlaySettings Clone()
        {
            return (OverlaySettings)MemberwiseClone();
        }

        /// <summary>
        /// Reprend du .ini (<paramref name="fresh"/>) les réglages que les raccourcis changent en jeu, sauf ceux
        /// modifiés ici depuis <paramref name="baseline"/> (dernier état lu ou enregistré). Retourne true si
        /// quelque chose a été repris.
        /// </summary>
        public bool MergeHotkeyState(OverlaySettings fresh, OverlaySettings baseline)
        {
            bool changed = false;
            if (Visible == baseline.Visible && Visible != fresh.Visible) { Visible = fresh.Visible; changed = true; }
            if (Position == baseline.Position && Position != fresh.Position) { Position = fresh.Position; changed = true; }
            if (Width == baseline.Width && Width != fresh.Width) { Width = fresh.Width; changed = true; }
            if (GlobalOpacity == baseline.GlobalOpacity && GlobalOpacity != fresh.GlobalOpacity) { GlobalOpacity = fresh.GlobalOpacity; changed = true; }
            if (WindowEnabled == baseline.WindowEnabled && WindowEnabled != fresh.WindowEnabled) { WindowEnabled = fresh.WindowEnabled; changed = true; }
            if (WindowAnchor == baseline.WindowAnchor && WindowAnchor != fresh.WindowAnchor) { WindowAnchor = fresh.WindowAnchor; changed = true; }
            return changed;
        }

        private static string Number(float value)
        {
            return value.ToString("0.00", CultureInfo.InvariantCulture);
        }

        private static string Hex(Color color)
        {
            return string.Format(CultureInfo.InvariantCulture, "#{0:X2}{1:X2}{2:X2}", color.R, color.G, color.B);
        }

        /// <summary>Enregistre les réglages dans le .ini (les commentaires et les autres sections sont conservés).</summary>
        public bool Save(string path)
        {
            bool ok = true;
            ok &= WritePrivateProfileStringW(Section, "visible", Visible ? "1" : "0", path);
            ok &= WritePrivateProfileStringW(Section, "position", PositionNames[Position], path);
            ok &= WritePrivateProfileStringW(Section, "distance", Number(Distance), path);
            ok &= WritePrivateProfileStringW(Section, "offset_x", Number(OffsetX), path);
            ok &= WritePrivateProfileStringW(Section, "offset_y", Number(OffsetY), path);
            ok &= WritePrivateProfileStringW(Section, "width", Number(Width), path);
            ok &= WritePrivateProfileStringW(Section, "opacity", Number(Opacity), path);
            ok &= WritePrivateProfileStringW(Section, "global_opacity", Number(GlobalOpacity), path);
            ok &= WritePrivateProfileStringW(Section, "face_viewer", FaceViewer ? "1" : "0", path);
            ok &= WritePrivateProfileStringW(Section, "clock", Clock ? "1" : "0", path);
            ok &= WritePrivateProfileStringW(Section, "headset_info", HeadsetInfo ? "1" : "0", path);
            ok &= WritePrivateProfileStringW(Section, "session", PanelSession ? "1" : "0", path);
            ok &= WritePrivateProfileStringW(Section, "battery", PanelBattery ? "1" : "0", path);
            ok &= WritePrivateProfileStringW(Section, "layout", LayoutNames[Layout], path);
            ok &= WritePrivateProfileStringW(Section, "theme", ThemeNames[Theme], path);
            ok &= WritePrivateProfileStringW(Section, "custom_background", Hex(CustomBackground), path);
            ok &= WritePrivateProfileStringW(Section, "custom_text", Hex(CustomText), path);
            ok &= WritePrivateProfileStringW(Section, "custom_accent", Hex(CustomAccent), path);
            ok &= WritePrivateProfileStringW("gpu", "measure", GpuTiming ? "1" : "0", path);
            ok &= WritePrivateProfileStringW("summary", "enabled", ReportsEnabled ? "1" : "0", path);
            ok &= WritePrivateProfileStringW("summary", "hardware", ReportHardware ? "1" : "0", path);
            ok &= WritePrivateProfileStringW("openvr", "enabled", OpenVR ? "1" : "0", path);
            ok &= WritePrivateProfileStringW(Section, "anchor", AnchorNames[Anchor], path);
            ok &= WritePrivateProfileStringW(Section, "hand_position", PlacementNames[HandPlacement], path);
            ok &= WritePrivateProfileStringW(Section, "gaze_only", GazeOnly ? "1" : "0", path);
            ok &= WritePrivateProfileStringW(Section, "hand_along", Number(HandAlong), path);
            ok &= WritePrivateProfileStringW(Section, "hand_lift", Number(HandLift), path);
            ok &= WritePrivateProfileStringW("watch", "enabled", WatchEnabled ? "1" : "0", path);
            ok &= WritePrivateProfileStringW("watch", "anchor", AnchorNames[WatchAnchor], path);
            ok &= WritePrivateProfileStringW("watch", "hand_position", PlacementNames[WatchPlacement], path);
            ok &= WritePrivateProfileStringW("watch", "style", WatchStyleNames[WatchStyle], path);
            ok &= WritePrivateProfileStringW("watch", "session", WatchSession ? "1" : "0", path);
            ok &= WritePrivateProfileStringW("watch", "battery", WatchBattery ? "1" : "0", path);
            ok &= WritePrivateProfileStringW("watch", "fps", WatchFps ? "1" : "0", path);
            ok &= WritePrivateProfileStringW("watch", "gaze_only", WatchGazeOnly ? "1" : "0", path);
            ok &= WritePrivateProfileStringW("watch", "size", Number(WatchSize), path);
            ok &= WritePrivateProfileStringW("watch", "opacity", Number(WatchOpacity), path);
            ok &= WritePrivateProfileStringW("watch", "hand_along", Number(WatchAlong), path);
            ok &= WritePrivateProfileStringW("watch", "hand_lift", Number(WatchLift), path);
            // La pose dans l'espace (« world ») est écrite par la layer et XRPerfVR (Ctrl+Alt+G) : jamais ici.
            ok &= WritePrivateProfileStringW("window", "enabled", WindowEnabled ? "1" : "0", path);
            ok &= WritePrivateProfileStringW("window", "anchor", WindowAnchorNames[WindowAnchor], path);
            ok &= WritePrivateProfileStringW("window", "hand_position", PlacementNames[WindowPlacement], path);
            ok &= WritePrivateProfileStringW("window", "gaze_only", WindowGazeOnly ? "1" : "0", path);
            ok &= WritePrivateProfileStringW("window", "width", Number(WindowWidth), path);
            ok &= WritePrivateProfileStringW("window", "hand_along", Number(WindowAlong), path);
            ok &= WritePrivateProfileStringW("window", "hand_lift", Number(WindowLift), path);
            ok &= WritePrivateProfileStringW("window", "distance", Number(WindowDistance), path);
            return ok;
        }

        private static void Put(byte[] block, int offset, uint value)
        {
            BitConverter.GetBytes(value).CopyTo(block, offset);
        }

        private static void Put(byte[] block, int offset, float value)
        {
            BitConverter.GetBytes(value).CopyTo(block, offset);
        }

        private static uint Packed(Color color)
        {
            return ((uint)color.R << 16) | ((uint)color.G << 8) | color.B;
        }

        /// <summary>
        /// Les réglages au format du bloc partagé avec la layer (SettingsBlock dans
        /// layer/src/shared_block.h : mêmes offsets, 184 octets).
        /// </summary>
        /// <param name="generation">Numéro de cet état des réglages.</param>
        /// <param name="changeTick">Environment.TickCount64 au dernier changement fait par l'utilisateur, 0 s'il n'y en a pas eu.</param>
        public byte[] ToBlock(uint generation, long changeTick)
        {
            var block = new byte[SettingsShare.Size];
            Put(block, 0, SettingsShare.Magic);
            Put(block, 4, SettingsShare.Version);
            Put(block, 8, 0u); // séquence : gérée par SettingsShare
            Put(block, 12, generation);
            Put(block, 16, Visible ? 1u : 0u);
            Put(block, 20, (uint)Position);
            Put(block, 24, Distance);
            Put(block, 28, OffsetX);
            Put(block, 32, OffsetY);
            Put(block, 36, Width);
            Put(block, 40, Opacity);
            Put(block, 44, GlobalOpacity);
            Put(block, 48, FaceViewer ? 1u : 0u);
            Put(block, 52, Clock ? 1u : 0u);
            Put(block, 56, HeadsetInfo ? 1u : 0u);
            Put(block, 60, (uint)Layout);
            Put(block, 64, (uint)Theme);
            Put(block, 68, Packed(CustomBackground));
            Put(block, 72, Packed(CustomText));
            Put(block, 76, Packed(CustomAccent));
            BitConverter.GetBytes(changeTick).CopyTo(block, 80);
            Put(block, 88, (uint)Anchor);
            Put(block, 92, (uint)HandPlacement);
            Put(block, 96, GazeOnly ? 1u : 0u);
            Put(block, 100, HandAlong);
            Put(block, 104, HandLift);
            Put(block, 108, WatchEnabled ? 1u : 0u);
            Put(block, 112, (uint)WatchAnchor);
            Put(block, 116, (uint)WatchPlacement);
            Put(block, 120, (uint)WatchStyle);
            Put(block, 124, (WatchSession ? 1u : 0u) | (WatchBattery ? 2u : 0u) | (WatchFps ? 4u : 0u));
            Put(block, 128, WatchGazeOnly ? 1u : 0u);
            Put(block, 132, WatchSize);
            Put(block, 136, WatchOpacity);
            Put(block, 140, WatchAlong);
            Put(block, 144, WatchLift);
            Put(block, 148, (PanelSession ? 1u : 0u) | (PanelBattery ? 2u : 0u)); // kWatchSession | kWatchBattery
            Put(block, 152, WindowEnabled ? 1u : 0u);
            Put(block, 156, (uint)WindowAnchor);
            Put(block, 160, (uint)WindowPlacement);
            Put(block, 164, WindowGazeOnly ? 1u : 0u);
            Put(block, 168, WindowWidth);
            Put(block, 172, WindowAlong);
            Put(block, 176, WindowLift);
            Put(block, 180, WindowDistance);
            return block;
        }
    }

    /// <summary>
    /// Mémoire partagée "Local\XRPerfOverlay.Settings" : les réglages en cours, que la layer relit
    /// quelques fois par seconde pour les appliquer dans un jeu déjà lancé.
    /// </summary>
    internal sealed class SettingsShare : IDisposable
    {
        public const int Size = 184;
        public const uint Magic = 0x53505258; // "XRPS"
        public const uint Version = 3;

        private const string Name = @"Local\XRPerfOverlay.Settings";
        private const int OffsetSequence = 8;
        private const int OffsetGeneration = 12;
        private const int OffsetChangeTick = 80;

        private const uint PAGE_READWRITE = 0x04;
        private const uint FILE_MAP_WRITE = 0x0002;
        private const uint SDDL_REVISION_1 = 1;
        private static readonly IntPtr InvalidHandle = new IntPtr(-1);

        // Lecture seule pour les jeux (non administrateurs), comme pour les capteurs.
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
        private long _changeTick; // instant du dernier changement fait par l'utilisateur (0 = aucun)

        /// <summary>Dernier numéro de réglages publié (repris d'un jeu en cours si la mémoire existait déjà).</summary>
        public uint Generation { get; private set; }

        public SettingsShare()
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

            _sequence = Marshal.ReadInt32(_view, OffsetSequence) & ~1; // reprend une séquence paire
            Generation = unchecked((uint)Marshal.ReadInt32(_view, OffsetGeneration));
            _changeTick = Marshal.ReadInt64(_view, OffsetChangeTick);
        }

        /// <summary>Publie les réglages : un jeu en cours les applique à son prochain relevé.</summary>
        /// <param name="settings">Réglages à publier.</param>
        /// <param name="userChange">
        /// true quand l'utilisateur vient de changer un réglage ; false pour la simple publication au
        /// lancement de ce programme. Un jeu qui démarre s'en sert pour savoir si ces réglages sont
        /// plus récents que le .ini qu'il vient de lire.
        /// </param>
        public void Publish(OverlaySettings settings, bool userChange)
        {
            if (_view == IntPtr.Zero) return;
            Generation = unchecked(Generation + 1);
            if (userChange) _changeTick = Environment.TickCount64;
            byte[] block = settings.ToBlock(Generation, _changeTick);

            // Séquence impaire pendant l'écriture : le lecteur sait qu'il doit relire.
            Marshal.WriteInt32(_view, OffsetSequence, ++_sequence);
            Thread.MemoryBarrier();
            Marshal.Copy(block, 0, _view, OffsetSequence);                                           // marque et version
            Marshal.Copy(block, OffsetGeneration, IntPtr.Add(_view, OffsetGeneration), Size - OffsetGeneration);
            Thread.MemoryBarrier();
            Marshal.WriteInt32(_view, OffsetSequence, ++_sequence);
        }

        public void Dispose()
        {
            if (_view != IntPtr.Zero)
            {
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

    /// <summary>
    /// Aperçu du panneau hors jeu : l'image est dessinée par la DLL de la layer elle-même (fonction
    /// xrpoRenderPreview), donc exactement comme dans le casque.
    /// </summary>
    internal sealed class OverlayPreview
    {
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int RenderPreview(byte[] settings, byte[] sensors, double seconds, int clockMinutes,
            [MarshalAs(UnmanagedType.LPStr)] string headsetName, [In, Out] byte[] pixels, uint capacity);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int RenderWatchPreview(byte[] settings, int clockSeconds, [In, Out] byte[] pixels, uint capacity);

        /// <summary>Côté de l'image de la montre (WatchFace::kSize dans la layer).</summary>
        public const int WatchSize = 320;

        private readonly string _layerDllPath;
        private readonly System.Diagnostics.Stopwatch _clock = System.Diagnostics.Stopwatch.StartNew();
        private RenderPreview _render;
        private RenderWatchPreview _renderWatch;
        private bool _loadTried;

        public OverlayPreview(string layerDllPath)
        {
            _layerDllPath = layerDllPath;
        }

        // La DLL n'est chargée qu'au premier aperçu demandé : tant que la fenêtre du bureau n'a jamais
        // servi hors jeu, ce programme ne la garde pas ouverte. Absente ou trop ancienne : pas d'aperçu.
        private void Load()
        {
            _loadTried = true;
            try
            {
                if (!File.Exists(_layerDllPath)) return;
                IntPtr library = NativeLibrary.Load(_layerDllPath);
                if (NativeLibrary.TryGetExport(library, "xrpoRenderPreview", out IntPtr function))
                    _render = Marshal.GetDelegateForFunctionPointer<RenderPreview>(function);
                if (NativeLibrary.TryGetExport(library, "xrpoRenderWatchPreview", out IntPtr watch))
                    _renderWatch = Marshal.GetDelegateForFunctionPointer<RenderWatchPreview>(watch);
            }
            catch (Exception)
            {
                _render = null;
            }
        }

        /// <summary>Capteurs au format du bloc partagé (SensorBlock, 56 octets), ou null s'ils ne sont pas encore lus.</summary>
        private static byte[] SensorBytes(Readings r)
        {
            if (r == null) return null;
            var block = new byte[56];
            BitConverter.GetBytes(0x4F505258u).CopyTo(block, 0); // "XRPO"
            BitConverter.GetBytes(1u).CopyTo(block, 4);
            float[] values = { r.CpuLoad, r.CpuTemp, r.CpuMHz, r.GpuLoad, r.GpuTemp, r.GpuMHz, r.VramUsedMB, r.VramTotalMB };
            for (int i = 0; i < values.Length; i++) BitConverter.GetBytes(values[i]).CopyTo(block, 24 + 4 * i);
            return block;
        }

        /// <summary>
        /// Dessine l'aperçu dans <paramref name="pixels"/> (RGBA, alpha prémultiplié, 640 x 320).
        /// Retourne la hauteur dessinée, ou 0 si l'aperçu n'est pas disponible.
        /// </summary>
        public int Render(OverlaySettings settings, Readings readings, byte[] pixels)
        {
            if (settings == null || pixels == null) return 0;
            if (!_loadTried) Load();
            if (_render == null) return 0;
            try
            {
                DateTime now = DateTime.Now;
                string name = string.IsNullOrEmpty(settings.HeadsetName) ? "Nom du casque" : settings.HeadsetName;
                return _render(settings.ToBlock(0, 0), SensorBytes(readings), _clock.Elapsed.TotalSeconds, now.Hour * 60 + now.Minute,
                    name, pixels, (uint)pixels.Length);
            }
            catch (Exception)
            {
                return 0;
            }
        }

        /// <summary>
        /// Dessine l'aperçu de la montre dans <paramref name="pixels"/> (RGBA, alpha prémultiplié, 320 x 320).
        /// Retourne le côté de l'image, ou 0 si l'aperçu n'est pas disponible (DLL absente ou trop ancienne).
        /// </summary>
        public int RenderWatch(OverlaySettings settings, byte[] pixels)
        {
            if (settings == null || pixels == null) return 0;
            if (!_loadTried) Load();
            if (_renderWatch == null) return 0;
            try
            {
                DateTime now = DateTime.Now;
                return _renderWatch(settings.ToBlock(0, 0), (now.Hour * 60 + now.Minute) * 60 + now.Second, pixels, (uint)pixels.Length);
            }
            catch (Exception)
            {
                return 0;
            }
        }
    }

    /// <summary>Fenêtre de réglages : casque, apparence, montre, fenêtre capturée, bureau, mesures et rapports.</summary>
    internal sealed class SettingsWindow : Form
    {
        private static readonly string[] PositionLabels = { "En haut à gauche", "En haut", "En haut à droite", "En bas à droite", "En bas", "En bas à gauche" };
        private static readonly string[] LayoutLabels = { "Complète (tableau CPU / GPU)", "Graphique (courbe du temps d'image)", "Compacte (FPS seulement)" };
        private static readonly string[] ThemeLabels = { "Sombre", "Clair", "Contraste élevé", "Vert", "Ambre", "Violet", "Personnalisées" };
        private static readonly string[] HandAnchorLabels = { "Devant les yeux", "Main gauche", "Main droite" };
        private static readonly string[] PlacementLabels = { "Dos du poignet (comme une montre)", "Intérieur du poignet", "Au-dessus de la manette" };
        private static readonly string[] WatchStyleLabels = { "Classique (aiguilles)", "Numérique", "Sport (chiffres et couronne)" };
        private static readonly string[] WindowAnchorLabels = { "Main gauche", "Main droite", "Posée dans l'espace (Ctrl+Alt+G la pose devant vous)" };
        private static readonly string[] AnchorLabels =
        {
            "Libre (se déplace à la souris)", "En haut à gauche de l'écran", "En haut de l'écran", "En haut à droite de l'écran",
            "En bas à droite de l'écran", "En bas de l'écran", "En bas à gauche de l'écran",
        };

        private readonly TabControl _tabs = new TabControl();
        private TableLayoutPanel _table; // tableau de l'onglet en cours de construction
        private OverlaySettings _overlay;
        private readonly Settings _desktop;
        private readonly Action _overlayChanged;
        private readonly Action _desktopChanged;
        private bool _loading;

        // Contrôles relus par LoadFrom.
        private CheckBox _panelSession, _panelBattery;
        private CheckBox _visible, _faceViewer, _clock, _headsetInfo, _desktopShown, _desktopTopMost, _gpuTiming, _reports, _reportHardware, _openVr;
        private Button _openReports;
        private ComboBox _position, _layout, _theme, _desktopAnchor;
        private TrackBar _width, _globalOpacity, _opacity, _distance, _offsetX, _offsetY, _desktopScale, _desktopOpacity;
        private Button _colorBackground, _colorText, _colorAccent;
        private FlowLayoutPanel _customColors;
        // Accroche du panneau à une main, et montre.
        private ComboBox _anchor, _handPlacement, _watchAnchor, _watchPlacement, _watchStyle;
        private CheckBox _gazeOnly, _watchEnabled, _watchSession, _watchBattery, _watchFps, _watchGazeOnly;
        private TrackBar _handAlong, _handLift, _watchSize, _watchOpacity, _watchAlong, _watchLift;
        private PictureBox _watchPreview;
        private readonly byte[] _watchPixels = new byte[OverlayPreview.WatchSize * OverlayPreview.WatchSize * 4];
        private readonly Func<OverlaySettings, byte[], int> _renderWatch;
        private readonly System.Windows.Forms.Timer _watchTimer = new System.Windows.Forms.Timer { Interval = 1000 };
        // Fenêtre capturée.
        private readonly WindowCapture _capture;
        private readonly Action _captureChanged;
        private TabPage _windowPage;
        private CheckBox _windowEnabled, _windowGazeOnly;
        private ComboBox _windowList, _windowAnchor, _windowPlacement;
        private Button _windowRefresh;
        private Label _windowStatus;
        private TrackBar _windowWidth, _windowDistance, _windowAlong, _windowLift, _captureOpacity, _captureFps;
        private NumericUpDown _cropLeft, _cropTop, _cropRight, _cropBottom;
        private FlowLayoutPanel _cropPanel, _cropPanel2;
        private PictureBox _windowPreview;
        private readonly System.Windows.Forms.Timer _windowTimer = new System.Windows.Forms.Timer { Interval = 250 };
        private bool _previewWasShown;

        /// <param name="overlay">Réglages du casque (modifiés sur place).</param>
        /// <param name="desktop">Réglages de la fenêtre du bureau (modifiés sur place).</param>
        /// <param name="overlayChanged">Appelé après chaque changement d'un réglage du casque.</param>
        /// <param name="desktopChanged">Appelé après chaque changement d'un réglage du bureau.</param>
        /// <param name="activated">Appelé quand la fenêtre revient au premier plan (pour relire le .ini).</param>
        /// <param name="openReports">Ouvre la fenêtre des rapports de session.</param>
        /// <param name="unavailableReason">Non nul : les réglages du casque ne peuvent pas être enregistrés, et pourquoi.</param>
        /// <param name="renderWatch">Dessine l'aperçu de la montre (voir OverlayPreview.RenderWatch), ou null.</param>
        /// <param name="capture">Capture de fenêtre (liste, état, aperçu), ou null si elle est indisponible.</param>
        /// <param name="captureChanged">Appelé quand la fenêtre choisie, les réglages de capture ou l'affichage de l'aperçu changent.</param>
        public SettingsWindow(OverlaySettings overlay, Settings desktop, Action overlayChanged, Action desktopChanged, Action activated,
            Action openReports, string unavailableReason, Func<OverlaySettings, byte[], int> renderWatch = null,
            WindowCapture capture = null, Action captureChanged = null)
        {
            _renderWatch = renderWatch;
            _capture = capture;
            _captureChanged = captureChanged;
            _overlay = overlay;
            _desktop = desktop;
            _overlayChanged = overlayChanged;
            _desktopChanged = desktopChanged;

            Text = "XR Perf Overlay " + AppVersion.Text + " - réglages";
            Icon = AppIcon.Load(new Size(32, 32));
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = true;
            ShowInTaskbar = true;
            StartPosition = FormStartPosition.CenterScreen;
            AutoScaleMode = AutoScaleMode.Font;
            AutoSize = true;
            AutoSizeMode = AutoSizeMode.GrowAndShrink;
            Padding = new Padding(10);

            _loading = true;
            BeginPage("Dans le casque");
            BuildHeadsetPage(unavailableReason);
            BeginPage("Apparence");
            BuildAppearancePage();
            BeginPage("Montre");
            BuildWatchPage();
            _windowPage = BeginPage("Fenêtre");
            BuildWindowPage();
            BeginPage("Sur le bureau");
            BuildDesktopPage();
            BeginPage("Mesures et rapports");
            BuildMeasuresPage(openReports);
            SizeTabs();

            // Onglets, remarque et boutons, les uns sous les autres.
            var outer = new TableLayoutPanel { ColumnCount = 1, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Dock = DockStyle.Fill };
            outer.Controls.Add(_tabs);
            outer.Controls.Add(new Label
            {
                Text = "Les changements s'appliquent tout de suite, y compris dans un jeu en cours, et sont enregistrés dans XRPerfOverlay.ini.",
                AutoSize = true,
                MaximumSize = new Size(_tabs.Width, 0),
                ForeColor = SystemColors.GrayText,
                Margin = new Padding(3, 8, 3, 4),
            });
            outer.Controls.Add(BuildButtons(unavailableReason == null));
            Controls.Add(outer);

            if (unavailableReason != null) SetHeadsetControlsEnabled(false);
            _loading = false;
            LoadFrom(overlay);
            // L'aperçu de la montre suit l'heure tant que la fenêtre est affichée.
            _watchTimer.Tick += (s, e) => RefreshWatchPreview();
            VisibleChanged += (s, e) => { if (Visible) _watchTimer.Start(); else _watchTimer.Stop(); PreviewStateChanged(); };
            // Aperçu de la fenêtre capturée : seulement quand son onglet est affiché.
            _windowTimer.Tick += (s, e) => RefreshWindowPreview();
            _tabs.SelectedIndexChanged += (s, e) => PreviewStateChanged();
            Resize += (s, e) => PreviewStateChanged();
            Activated += (s, e) =>
            {
                if (!_loading && activated != null) activated();
            };
        }

        // --- Construction ---------------------------------------------------------------------------

        private TabPage BeginPage(string title)
        {
            // AutoScroll : si le contenu dépassait malgré tout, des barres de défilement apparaîtraient au lieu d'une coupure.
            var page = new TabPage(title) { Padding = new Padding(8), AutoScroll = true, UseVisualStyleBackColor = true };
            _table = new TableLayoutPanel { ColumnCount = 3, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Location = new Point(8, 8) };
            _table.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            _table.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 260));
            _table.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 70));
            page.Controls.Add(_table);
            _tabs.TabPages.Add(page);
            return page;
        }

        // Les onglets prennent la taille du plus grand des trois contenus, plus leurs marges et la rangée d'onglets.
        private void SizeTabs()
        {
            int width = 0, height = 0;
            foreach (TabPage page in _tabs.TabPages)
            {
                Size size = page.Controls[0].GetPreferredSize(Size.Empty);
                width = Math.Max(width, size.Width);
                height = Math.Max(height, size.Height);
            }
            _tabs.Size = new Size(width + 44, height + 66);
            _tabs.Margin = new Padding(3);
        }

        private void AddRow(Control left, Control middle, Control right)
        {
            int row = _table.RowCount++;
            _table.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            if (left != null) _table.Controls.Add(left, 0, row);
            if (middle != null) _table.Controls.Add(middle, 1, row);
            if (right != null) _table.Controls.Add(right, 2, row);
        }

        private void AddNote(string text, Color color)
        {
            var label = new Label { Text = text, AutoSize = true, MaximumSize = new Size(480, 0), ForeColor = color, Margin = new Padding(3, 3, 3, 8) };
            AddRow(label, null, null);
            _table.SetColumnSpan(label, 3);
        }

        private static Label Caption(string text)
        {
            return new Label { Text = text, AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(3, 6, 12, 6) };
        }

        private TrackBar AddSlider(string caption, int minimum, int maximum, Func<int, string> format, Action<int> changed)
        {
            var value = new Label { AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(3, 6, 3, 6) };
            var slider = new TrackBar
            {
                Minimum = minimum,
                Maximum = maximum,
                TickStyle = TickStyle.None,
                SmallChange = 1,
                LargeChange = Math.Max(1, (maximum - minimum) / 20),
                AutoSize = false,
                Height = 28,
                Dock = DockStyle.Fill,
                Margin = new Padding(0, 3, 0, 3),
            };
            slider.ValueChanged += (s, e) =>
            {
                value.Text = format(slider.Value);
                if (!_loading) changed(slider.Value);
            };
            value.Text = format(slider.Value);
            AddRow(Caption(caption), slider, value);
            return slider;
        }

        private ComboBox AddCombo(string caption, string[] items, Action<int> changed)
        {
            var combo = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Dock = DockStyle.Fill, Margin = new Padding(3, 3, 3, 3) };
            combo.Items.AddRange(items);
            combo.SelectedIndexChanged += (s, e) =>
            {
                if (!_loading && combo.SelectedIndex >= 0) changed(combo.SelectedIndex);
            };
            AddRow(Caption(caption), combo, null);
            return combo;
        }

        private CheckBox AddCheck(string text, Action<bool> changed)
        {
            var check = new CheckBox { Text = text, AutoSize = true, Margin = new Padding(3, 4, 3, 4) };
            check.CheckedChanged += (s, e) =>
            {
                if (!_loading) changed(check.Checked);
            };
            AddRow(check, null, null);
            _table.SetColumnSpan(check, 3);
            return check;
        }

        private Button ColorButton(string text, Func<Color> get, Action<Color> set)
        {
            var button = new Button { Text = text, AutoSize = true, FlatStyle = FlatStyle.Flat, Margin = new Padding(0, 0, 8, 0) };
            button.Click += (s, e) =>
            {
                using (var dialog = new ColorDialog { Color = get(), FullOpen = true })
                {
                    if (dialog.ShowDialog(this) != DialogResult.OK) return;
                    set(dialog.Color);
                    Tint(button, dialog.Color);
                    _overlayChanged();
                }
            };
            return button;
        }

        // Le bouton prend la couleur choisie ; son texte reste lisible dessus.
        private static void Tint(Button button, Color color)
        {
            button.BackColor = color;
            button.ForeColor = (color.R * 299 + color.G * 587 + color.B * 114) / 1000 > 140 ? Color.Black : Color.White;
        }

        private void BuildHeadsetPage(string unavailableReason)
        {
            if (unavailableReason != null) AddNote(unavailableReason, Color.Firebrick);

            _visible = AddCheck("Afficher le panneau (Ctrl+Alt+O en jeu)", v => { _overlay.Visible = v; _overlayChanged(); });
            _position = AddCombo("Emplacement", PositionLabels, v => { _overlay.Position = v; _overlayChanged(); });
            _width = AddSlider("Taille (largeur du panneau)", 10, 150, v => v + " cm", v => { _overlay.Width = v / 100f; _overlayChanged(); });
            _globalOpacity = AddSlider("Opacité de l'ensemble", 20, 100, v => v + " %", v => { _overlay.GlobalOpacity = v / 100f; _overlayChanged(); });
            _opacity = AddSlider("Opacité du fond seul", 0, 100, v => v + " %", v => { _overlay.Opacity = v / 100f; _overlayChanged(); });
            _distance = AddSlider("Distance devant les yeux", 30, 500, v => (v / 100.0).ToString("0.00", CultureInfo.InvariantCulture) + " m",
                v => { _overlay.Distance = v / 100f; _overlayChanged(); });
            _offsetX = AddSlider("Écart horizontal", 0, 300, v => v + " cm", v => { _overlay.OffsetX = v / 100f; _overlayChanged(); });
            _offsetY = AddSlider("Écart vertical", 0, 300, v => v + " cm", v => { _overlay.OffsetY = v / 100f; _overlayChanged(); });
            _faceViewer = AddCheck("Tourner le panneau vers les yeux", v => { _overlay.FaceViewer = v; _overlayChanged(); });

            // Accroche à une main : remplace l'emplacement devant les yeux.
            _anchor = AddCombo("Accroché à", HandAnchorLabels, v => { _overlay.Anchor = v; UpdateHandControls(); _overlayChanged(); });
            _handPlacement = AddCombo("Sur la main", PlacementLabels, v => { _overlay.HandPlacement = v; _overlayChanged(); });
            _gazeOnly = AddCheck("Seulement quand je regarde ma main", v => { _overlay.GazeOnly = v; _overlayChanged(); });
            _handAlong = AddSlider("Le long de l'avant-bras", -12, 12, Centimeters, v => { _overlay.HandAlong = v / 100f; _overlayChanged(); });
            _handLift = AddSlider("Écart à la main", -5, 10, Centimeters, v => { _overlay.HandLift = v / 100f; _overlayChanged(); });

            _openVr = AddCheck("Aussi dans les jeux SteamVR (OpenVR)", v => { _overlay.OpenVR = v; _overlayChanged(); });
            AddNote("Panneau, montre, rapports et fenêtre du bureau dans les jeux qui passent par SteamVR sans OpenXR. Un petit programme, " +
                    "XRPerfVR, est lancé par XRPerfSensors et s'ajoute à SteamVR quand celui-ci est ouvert (il ne le démarre jamais). " +
                    "Le temps GPU y est mesuré par SteamVR. Les jeux OpenXR restent pris en charge par la layer.", SystemColors.GrayText);
        }

        private static string Centimeters(int value)
        {
            return (value > 0 ? "+" : "") + value + " cm";
        }

        private void BuildWatchPage()
        {
            AddNote("Une petite montre en plus du panneau : heure, temps de session, batterie des manettes et FPS. Au poignet, elle suit " +
                    "la manette ; le suivi des manettes est mis en place au lancement du jeu (si vous l'activez en cours de partie, relancez le " +
                    "jeu). Si le jeu ne déclare pas de manettes, la montre s'affiche en bas à droite, devant les yeux.", SystemColors.GrayText);
            _watchEnabled = AddCheck("Afficher la montre", v => { _overlay.WatchEnabled = v; WatchChanged(); });
            _watchAnchor = AddCombo("Accrochée à", HandAnchorLabels, v => { _overlay.WatchAnchor = v; UpdateHandControls(); WatchChanged(); });
            _watchPlacement = AddCombo("Sur la main", PlacementLabels, v => { _overlay.WatchPlacement = v; WatchChanged(); });
            _watchStyle = AddCombo("Style", WatchStyleLabels, v => { _overlay.WatchStyle = v; WatchChanged(); });

            _watchSession = new CheckBox { Text = "Temps de session", AutoSize = true, Margin = new Padding(0, 4, 12, 4) };
            _watchBattery = new CheckBox { Text = "Batterie des manettes", AutoSize = true, Margin = new Padding(0, 4, 12, 4) };
            _watchFps = new CheckBox { Text = "FPS", AutoSize = true, Margin = new Padding(0, 4, 0, 4) };
            _watchSession.CheckedChanged += (s, e) => { if (!_loading) { _overlay.WatchSession = _watchSession.Checked; WatchChanged(); } };
            _watchBattery.CheckedChanged += (s, e) => { if (!_loading) { _overlay.WatchBattery = _watchBattery.Checked; WatchChanged(); } };
            _watchFps.CheckedChanged += (s, e) => { if (!_loading) { _overlay.WatchFps = _watchFps.Checked; WatchChanged(); } };
            var elements = new FlowLayoutPanel { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, WrapContents = false, Margin = new Padding(3, 0, 3, 0) };
            elements.Controls.AddRange(new Control[] { _watchSession, _watchBattery, _watchFps });
            AddRow(Caption("Afficher aussi"), elements, null);
            _table.SetColumnSpan(elements, 2);

            _watchGazeOnly = AddCheck("Seulement quand je regarde mon poignet", v => { _overlay.WatchGazeOnly = v; WatchChanged(); });
            _watchSize = AddSlider("Taille", 3, 20, v => v + " cm", v => { _overlay.WatchSize = v / 100f; WatchChanged(); });
            _watchOpacity = AddSlider("Opacité", 20, 100, v => v + " %", v => { _overlay.WatchOpacity = v / 100f; WatchChanged(); });
            _watchAlong = AddSlider("Le long de l'avant-bras", -12, 12, Centimeters, v => { _overlay.WatchAlong = v / 100f; WatchChanged(); });
            _watchLift = AddSlider("Écart au poignet", -5, 10, Centimeters, v => { _overlay.WatchLift = v / 100f; WatchChanged(); });
            AddNote("Batterie : affichée quand le runtime OpenXR la fournit (extension récente, encore rare sur PC) ; sinon, elle n'apparaît pas.",
                    SystemColors.GrayText);

            // Aperçu, dessiné par la DLL de la layer comme dans le casque (couleurs de l'onglet Apparence).
            _watchPreview = new PictureBox { Size = new Size(160, 160), SizeMode = PictureBoxSizeMode.Zoom, Margin = new Padding(3, 4, 3, 4), Anchor = AnchorStyles.None };
            AddRow(_watchPreview, null, null);
            _table.SetColumnSpan(_watchPreview, 3);
        }

        private void BuildWindowPage()
        {
            AddNote("Une fenêtre du bureau (chat d'un stream, Discord...) affichée dans le casque, en lecture seule. La capture ne tourne " +
                    "que pendant un jeu VR, et pendant l'aperçu ci-dessous. En jeu : Ctrl+Alt+F l'affiche ou la masque, Ctrl+Alt+G la pose " +
                    "devant vous. Une fenêtre réduite n'est plus dessinée par Windows : laissez-la ouverte, même derrière d'autres.",
                    SystemColors.GrayText);
            _windowEnabled = AddCheck("Afficher la fenêtre dans le casque (Ctrl+Alt+F en jeu)", v => { _overlay.WindowEnabled = v; _overlayChanged(); });

            _windowList = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Dock = DockStyle.Fill, Margin = new Padding(3, 3, 3, 3) };
            _windowList.SelectedIndexChanged += (s, e) =>
            {
                if (_loading || !(_windowList.SelectedItem is DesktopWindow chosen) || chosen.Handle == IntPtr.Zero) return;
                _desktop.CaptureTitle = chosen.Title;
                _desktop.CaptureProcess = chosen.Process;
                if (_capture != null) _capture.Retarget(chosen.Handle);
                CaptureChanged();
            };
            AddRow(Caption("Fenêtre à afficher"), _windowList, null);
            _table.SetColumnSpan(_windowList, 2);
            _windowRefresh = new Button { Text = "Actualiser la liste", AutoSize = true, Margin = new Padding(3, 0, 3, 3) };
            _windowRefresh.Click += (s, e) => FillWindowList();
            AddRow(null, _windowRefresh, null);
            _windowStatus = new Label { AutoSize = true, MaximumSize = new Size(480, 0), ForeColor = SystemColors.GrayText, Margin = new Padding(3, 3, 3, 6) };
            AddRow(_windowStatus, null, null);
            _table.SetColumnSpan(_windowStatus, 3);

            _windowAnchor = AddCombo("Emplacement", WindowAnchorLabels, v => { _overlay.WindowAnchor = v; UpdateHandControls(); _overlayChanged(); });
            _windowDistance = AddSlider("Distance quand je la pose", 30, 300, v => (v / 100.0).ToString("0.00", CultureInfo.InvariantCulture) + " m",
                v => { _overlay.WindowDistance = v / 100f; _overlayChanged(); });
            _windowPlacement = AddCombo("Sur la main", PlacementLabels, v => { _overlay.WindowPlacement = v; _overlayChanged(); });
            _windowGazeOnly = AddCheck("Seulement quand je regarde ma main", v => { _overlay.WindowGazeOnly = v; _overlayChanged(); });
            _windowAlong = AddSlider("Le long de l'avant-bras", -12, 12, Centimeters, v => { _overlay.WindowAlong = v / 100f; _overlayChanged(); });
            _windowLift = AddSlider("Écart à la main", -5, 10, Centimeters, v => { _overlay.WindowLift = v / 100f; _overlayChanged(); });
            _windowWidth = AddSlider("Taille (largeur)", 10, 150, v => v + " cm", v => { _overlay.WindowWidth = v / 100f; _overlayChanged(); });
            _captureOpacity = AddSlider("Opacité", Settings.MinOpacity, 100, v => v + " %", v => { _desktop.CaptureOpacity = v; CaptureChanged(); });
            _captureFps = AddSlider("Images par seconde", Settings.MinCaptureFps, Settings.MaxCaptureFps, v => v.ToString(CultureInfo.InvariantCulture),
                v => { _desktop.CaptureFps = v; CaptureChanged(); });

            // Recadrage : part retirée de chaque côté, par exemple pour ne garder que la colonne du chat d'une page.
            _cropLeft = CropBox(v => _desktop.CropLeft = v);
            _cropTop = CropBox(v => _desktop.CropTop = v);
            _cropRight = CropBox(v => _desktop.CropRight = v);
            _cropBottom = CropBox(v => _desktop.CropBottom = v);
            // Deux lignes : sur une seule, la dernière case sortait de la fenêtre.
            _cropPanel = CropRow(Tuple.Create("gauche", _cropLeft), Tuple.Create("droite", _cropRight));
            AddRow(Caption("Recadrage (%)"), _cropPanel, null);
            _table.SetColumnSpan(_cropPanel, 2);
            _cropPanel2 = CropRow(Tuple.Create("haut", _cropTop), Tuple.Create("bas", _cropBottom));
            AddRow(null, _cropPanel2, null);
            _table.SetColumnSpan(_cropPanel2, 2);

            _windowPreview = new PictureBox
            {
                Size = new Size(240, 150),
                SizeMode = PictureBoxSizeMode.Zoom,
                BorderStyle = BorderStyle.FixedSingle,
                BackColor = Color.FromArgb(40, 40, 40),
                Margin = new Padding(3, 6, 3, 4),
                Anchor = AnchorStyles.None,
            };
            AddRow(_windowPreview, null, null);
            _table.SetColumnSpan(_windowPreview, 3);
            if (_capture == null)
            {
                _windowStatus.Text = "Capture de fenêtre indisponible sur cette installation.";
                foreach (Control control in new Control[] { _windowList, _windowRefresh, _captureOpacity, _captureFps, _cropPanel, _cropPanel2 }) control.Enabled = false;
            }
        }

        private FlowLayoutPanel CropRow(params Tuple<string, NumericUpDown>[] parts)
        {
            var row = new FlowLayoutPanel { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, WrapContents = false, Margin = new Padding(3, 0, 3, 0) };
            foreach (var part in parts)
            {
                row.Controls.Add(new Label { Text = part.Item1, AutoSize = true, MinimumSize = new Size(48, 0), Margin = new Padding(0, 6, 2, 0) });
                row.Controls.Add(part.Item2);
            }
            return row;
        }

        private NumericUpDown CropBox(Action<int> set)
        {
            var box = new NumericUpDown { Minimum = 0, Maximum = Settings.MaxCrop, Width = 52, Margin = new Padding(0, 3, 16, 3) };
            box.ValueChanged += (s, e) =>
            {
                if (_loading) return;
                set((int)box.Value);
                CaptureChanged();
            };
            // Un nombre tapé au clavier est pris en compte tout de suite, sans attendre de quitter la case
            // (lire Value valide le texte saisi et déclenche ValueChanged).
            box.KeyUp += (s, e) => { decimal unused = box.Value; };
            return box;
        }

        private void CaptureChanged()
        {
            _desktop.Save();
            if (_captureChanged != null) _captureChanged();
            RefreshWindowPreview();
        }

        /// <summary>L'aperçu de la fenêtre capturée est à l'écran (la capture doit alors tourner).</summary>
        public bool WindowPreviewShown
        {
            get { return Visible && WindowState != FormWindowState.Minimized && _tabs.SelectedTab == _windowPage && _capture != null; }
        }

        private void PreviewStateChanged()
        {
            bool shown = WindowPreviewShown;
            if (shown == _previewWasShown) return;
            _previewWasShown = shown;
            if (shown)
            {
                FillWindowList();
                if (_capture != null) _capture.ResetPreview();
                _windowTimer.Start();
            }
            else
            {
                _windowTimer.Stop();
            }
            if (_captureChanged != null) _captureChanged();
            RefreshWindowPreview();
        }

        // Fenêtres ouvertes ; la fenêtre choisie reste en tête de liste, même fermée.
        private void FillWindowList()
        {
            if (_windowList == null || _capture == null) return;
            bool wasLoading = _loading;
            _loading = true;
            try
            {
                _windowList.BeginUpdate();
                _windowList.Items.Clear();
                var windows = WindowCapture.ListWindows();
                IntPtr current = WindowCapture.Find(_desktop);
                DesktopWindow selected = null;
                foreach (DesktopWindow w in windows)
                {
                    _windowList.Items.Add(w);
                    if (w.Handle == current) selected = w;
                }
                if (selected == null && (_desktop.CaptureTitle.Length > 0 || _desktop.CaptureProcess.Length > 0))
                {
                    selected = new DesktopWindow { Title = _desktop.CaptureTitle + " (fermée)", Process = _desktop.CaptureProcess };
                    _windowList.Items.Insert(0, selected);
                }
                if (selected != null) _windowList.SelectedItem = selected;
            }
            finally
            {
                _windowList.EndUpdate();
                _loading = wasLoading;
            }
        }

        private void RefreshWindowPreview()
        {
            if (_windowStatus == null || _capture == null) return;
            string status = _capture.StatusText();
            if (status.Length == 0) status = _overlay.WindowEnabled ? "" : "Affichage dans le casque désactivé (case ci-dessus) : seul l'aperçu tourne.";
            if (_windowStatus.Text != status) _windowStatus.Text = status;
            if (!WindowPreviewShown) return;
            Bitmap image = _capture.TakePreview();
            if (image == null) return;
            Image old = _windowPreview.Image;
            _windowPreview.Image = image;
            if (old != null) old.Dispose();
        }

        private void WatchChanged()
        {
            RefreshWatchPreview();
            _overlayChanged();
        }

        // Les réglages propres à une main ne servent que si l'accroche est une main.
        private void UpdateHandControls()
        {
            if (_anchor == null || _watchAnchor == null || !_visible.Enabled) return;
            bool panelOnHand = _overlay.Anchor != 0, watchOnHand = _overlay.WatchAnchor != 0;
            foreach (Control control in new Control[] { _handPlacement, _gazeOnly, _handAlong, _handLift }) control.Enabled = panelOnHand;
            foreach (Control control in new Control[] { _position, _distance, _offsetX, _offsetY, _faceViewer }) control.Enabled = !panelOnHand;
            foreach (Control control in new Control[] { _watchPlacement, _watchGazeOnly, _watchAlong, _watchLift }) control.Enabled = watchOnHand;
            bool windowOnHand = _overlay.WindowAnchor != OverlaySettings.WindowWorld;
            foreach (Control control in new Control[] { _windowPlacement, _windowGazeOnly, _windowAlong, _windowLift }) control.Enabled = windowOnHand;
            _windowDistance.Enabled = !windowOnHand;
        }

        private void RefreshWatchPreview()
        {
            if (_watchPreview == null || _renderWatch == null || !Visible) return;
            if (_renderWatch(_overlay, _watchPixels) <= 0) return;
            const int size = OverlayPreview.WatchSize;
            var bitmap = new Bitmap(size, size, System.Drawing.Imaging.PixelFormat.Format32bppPArgb);
            System.Drawing.Imaging.BitmapData data = bitmap.LockBits(new Rectangle(0, 0, size, size), System.Drawing.Imaging.ImageLockMode.WriteOnly,
                System.Drawing.Imaging.PixelFormat.Format32bppPArgb);
            try
            {
                // RGBA (layer) -> BGRA (Windows), alpha prémultiplié dans les deux cas.
                var row = new byte[size * 4];
                for (int y = 0; y < size; y++)
                {
                    for (int x = 0; x < size; x++)
                    {
                        int i = (y * size + x) * 4;
                        row[x * 4] = _watchPixels[i + 2];
                        row[x * 4 + 1] = _watchPixels[i + 1];
                        row[x * 4 + 2] = _watchPixels[i];
                        row[x * 4 + 3] = _watchPixels[i + 3];
                    }
                    Marshal.Copy(row, 0, IntPtr.Add(data.Scan0, y * data.Stride), row.Length);
                }
            }
            finally
            {
                bitmap.UnlockBits(data);
            }
            Image old = _watchPreview.Image;
            _watchPreview.Image = bitmap;
            if (old != null) old.Dispose();
        }

        private void BuildAppearancePage()
        {
            AddNote("Ces choix valent pour le casque et pour la fenêtre du bureau.", SystemColors.GrayText);
            _layout = AddCombo("Présentation", LayoutLabels, v => { _overlay.Layout = v; _overlayChanged(); });
            _theme = AddCombo("Couleurs", ThemeLabels, v =>
            {
                _overlay.Theme = v;
                _customColors.Enabled = v == OverlaySettings.CustomTheme;
                _overlayChanged();
            });

            _colorBackground = ColorButton("Fond", () => _overlay.CustomBackground, c => _overlay.CustomBackground = c);
            _colorText = ColorButton("Texte", () => _overlay.CustomText, c => _overlay.CustomText = c);
            _colorAccent = ColorButton("Accent", () => _overlay.CustomAccent, c => _overlay.CustomAccent = c);
            _customColors = new FlowLayoutPanel { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, WrapContents = false, Margin = new Padding(3, 3, 3, 3) };
            _customColors.Controls.Add(_colorBackground);
            _customColors.Controls.Add(_colorText);
            _customColors.Controls.Add(_colorAccent);
            AddRow(Caption("Couleurs personnalisées"), _customColors, null);
            _table.SetColumnSpan(_customColors, 2);

            _clock = AddCheck("Afficher l'heure", v => { _overlay.Clock = v; _overlayChanged(); });
            _headsetInfo = AddCheck("Afficher le casque et la résolution de rendu", v => { _overlay.HeadsetInfo = v; _overlayChanged(); });
            _panelSession = AddCheck("Afficher le temps de session", v => { _overlay.PanelSession = v; _overlayChanged(); });
            _panelBattery = AddCheck("Afficher la batterie des manettes", v => { _overlay.PanelBattery = v; _overlayChanged(); });
            AddNote("Ligne en bas du panneau. La batterie vient du runtime : SteamVR la donne, Pimax OpenXR pas encore " +
                    "(« batterie indisponible »). Dans un jeu OpenXR, la batterie demande de relancer le jeu après avoir coché la case.",
                    SystemColors.GrayText);
        }

        private void BuildDesktopPage()
        {
            AddNote("Fenêtre miroir du panneau, à capturer dans OBS. Sans jeu VR en cours, elle montre un aperçu de vos réglages.", SystemColors.GrayText);
            _desktopShown = AddCheck("Afficher la fenêtre", v => { _desktop.DesktopPanel = v; _desktopChanged(); });
            _desktopAnchor = AddCombo("Emplacement", AnchorLabels, v => { _desktop.Anchor = v; _desktopChanged(); });
            _desktopScale = AddSlider("Taille", Settings.MinScale, Settings.MaxScale, v => v + " %", v => { _desktop.Scale = v; _desktopChanged(); });
            _desktopOpacity = AddSlider("Opacité", Settings.MinOpacity, 100, v => v + " %", v => { _desktop.Opacity = v; _desktopChanged(); });
            _desktopTopMost = AddCheck("Garder la fenêtre au premier plan", v => { _desktop.TopMost = v; _desktopChanged(); });
        }

        private void BuildMeasuresPage(Action openReports)
        {
            _gpuTiming = AddCheck("Mesurer le vrai temps GPU", v => { _overlay.GpuTiming = v; _overlayChanged(); });
            AddNote("Désactivé : le temps GPU est estimé (charge de la carte x durée d'une image), affiché précédé de « ~ », et rien n'est " +
                    "ajouté au rendu du jeu. Activé : deux horodatages par image sont ajoutés au travail GPU du jeu (Direct3D 11 et 12) ; " +
                    "le temps mesuré remplace alors l'estimation dans le panneau, la courbe et le rapport. C'est l'écart entre le début et la fin de " +
                    "chaque image sur le GPU : quand le processeur est la limite, il compte aussi les moments où la carte attend. " +
                    "Avec certains jeux (Unreal en Direct3D 12, UEVR), le rendu échappe à ces repères : la mesure, invraisemblable, est alors " +
                    "écartée d'elle-même au bout de 10 secondes et l'estimation reprend. " +
                    "Pris en compte au prochain lancement d'un jeu.",
                    SystemColors.GrayText);
            _reports = AddCheck("Écrire un rapport à chaque session de jeu", v => { _overlay.ReportsEnabled = v; _overlayChanged(); });
            AddNote("Un fichier texte par session de plus de 15 secondes, dans le dossier des rapports (« sessions » dans le dossier " +
                    "d'installation, sauf choix contraire dans le .ini) : FPS, temps par image, charges, températures, relevé minute " +
                    "par minute. Pris en compte au prochain lancement d'un jeu.", SystemColors.GrayText);
            _reportHardware = AddCheck("Indiquer le modèle du processeur et de la carte graphique dans les rapports",
                v => { _overlay.ReportHardware = v; _overlayChanged(); });
            AddNote("Utile pour comparer des sessions faites sur des PC différents. Décochez-le si vous ne voulez pas que vos rapports " +
                    "décrivent votre matériel, par exemple avant de les partager.", SystemColors.GrayText);
            _openReports = new Button { Text = "Ouvrir les rapports...", AutoSize = true, Margin = new Padding(3, 6, 3, 3) };
            _openReports.Click += (s, e) => { if (openReports != null) openReports(); };
            AddRow(_openReports, null, null);
            _table.SetColumnSpan(_openReports, 3);
        }

        private FlowLayoutPanel BuildButtons(bool headsetAvailable)
        {
            var defaults = new Button { Text = "Valeurs par défaut (casque et apparence)", AutoSize = true, Margin = new Padding(3, 6, 8, 3), Enabled = headsetAvailable };
            defaults.Click += (s, e) =>
            {
                var fresh = new OverlaySettings
                {
                    HeadsetName = _overlay.HeadsetName,
                    GpuTiming = _overlay.GpuTiming,
                    ReportsEnabled = _overlay.ReportsEnabled,
                    ReportHardware = _overlay.ReportHardware,
                    SummaryFolder = _overlay.SummaryFolder,
                    OpenVR = _overlay.OpenVR,
                };
                // La montre et la fenêtre capturée ont leurs propres réglages : ce bouton ne les touche pas.
                CopyWatch(_overlay, fresh);
                CopyWindow(_overlay, fresh);
                CopyInto(fresh, _overlay);
                LoadFrom(_overlay);
                _overlayChanged();
            };
            var close = new Button { Text = "Fermer", AutoSize = true, Margin = new Padding(0, 6, 3, 3) };
            close.Click += (s, e) => Hide();
            var buttons = new FlowLayoutPanel { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, WrapContents = false, Anchor = AnchorStyles.Right, Margin = new Padding(0) };
            buttons.Controls.Add(defaults);
            buttons.Controls.Add(close);
            CancelButton = close;
            return buttons;
        }

        private void SetHeadsetControlsEnabled(bool enabled)
        {
            foreach (Control control in new Control[]
            {
                _visible, _position, _width, _globalOpacity, _opacity, _distance, _offsetX, _offsetY, _faceViewer,
                _layout, _theme, _customColors, _clock, _headsetInfo, _panelSession, _panelBattery, _gpuTiming, _reports, _reportHardware, _openVr,
                _anchor, _handPlacement, _gazeOnly, _handAlong, _handLift, _watchEnabled, _watchAnchor, _watchPlacement, _watchStyle,
                _watchSession, _watchBattery, _watchFps, _watchGazeOnly, _watchSize, _watchOpacity, _watchAlong, _watchLift,
                _windowEnabled, _windowAnchor, _windowPlacement, _windowGazeOnly, _windowAlong, _windowLift, _windowWidth, _windowDistance,
            })
            {
                control.Enabled = enabled;
            }
        }

        // --- Échanges avec le modèle ------------------------------------------------------------------

        private static void CopyInto(OverlaySettings from, OverlaySettings to)
        {
            to.Visible = from.Visible; to.Position = from.Position; to.Distance = from.Distance; to.OffsetX = from.OffsetX; to.OffsetY = from.OffsetY;
            to.Width = from.Width; to.Opacity = from.Opacity; to.GlobalOpacity = from.GlobalOpacity; to.FaceViewer = from.FaceViewer; to.Clock = from.Clock;
            to.HeadsetInfo = from.HeadsetInfo; to.PanelSession = from.PanelSession; to.PanelBattery = from.PanelBattery; to.Layout = from.Layout; to.Theme = from.Theme; to.CustomBackground = from.CustomBackground;
            to.CustomText = from.CustomText; to.CustomAccent = from.CustomAccent; to.HeadsetName = from.HeadsetName;
            to.GpuTiming = from.GpuTiming; to.ReportsEnabled = from.ReportsEnabled; to.ReportHardware = from.ReportHardware;
            to.SummaryFolder = from.SummaryFolder;
            to.OpenVR = from.OpenVR;
            to.Anchor = from.Anchor; to.HandPlacement = from.HandPlacement; to.GazeOnly = from.GazeOnly; to.HandAlong = from.HandAlong; to.HandLift = from.HandLift;
            CopyWatch(from, to);
            CopyWindow(from, to);
        }

        private static void CopyWindow(OverlaySettings from, OverlaySettings to)
        {
            to.WindowEnabled = from.WindowEnabled; to.WindowAnchor = from.WindowAnchor; to.WindowPlacement = from.WindowPlacement; to.WindowGazeOnly = from.WindowGazeOnly;
            to.WindowWidth = from.WindowWidth; to.WindowAlong = from.WindowAlong; to.WindowLift = from.WindowLift; to.WindowDistance = from.WindowDistance;
        }

        private static void CopyWatch(OverlaySettings from, OverlaySettings to)
        {
            to.WatchEnabled = from.WatchEnabled; to.WatchAnchor = from.WatchAnchor; to.WatchPlacement = from.WatchPlacement; to.WatchStyle = from.WatchStyle;
            to.WatchSession = from.WatchSession; to.WatchBattery = from.WatchBattery; to.WatchFps = from.WatchFps; to.WatchGazeOnly = from.WatchGazeOnly;
            to.WatchSize = from.WatchSize; to.WatchOpacity = from.WatchOpacity; to.WatchAlong = from.WatchAlong; to.WatchLift = from.WatchLift;
        }

        private static void SetSlider(TrackBar slider, double value)
        {
            slider.Value = Math.Min(slider.Maximum, Math.Max(slider.Minimum, (int)Math.Round(value)));
        }

        /// <summary>Affiche les réglages donnés (par exemple après une relecture du .ini) sans déclencher d'envoi.</summary>
        public void LoadFrom(OverlaySettings overlay)
        {
            _overlay = overlay;
            _loading = true;
            try
            {
                _visible.Checked = overlay.Visible;
                _position.SelectedIndex = overlay.Position;
                SetSlider(_width, overlay.Width * 100);
                SetSlider(_globalOpacity, overlay.GlobalOpacity * 100);
                SetSlider(_opacity, overlay.Opacity * 100);
                SetSlider(_distance, overlay.Distance * 100);
                SetSlider(_offsetX, overlay.OffsetX * 100);
                SetSlider(_offsetY, overlay.OffsetY * 100);
                _faceViewer.Checked = overlay.FaceViewer;
                _layout.SelectedIndex = overlay.Layout;
                _theme.SelectedIndex = overlay.Theme;
                Tint(_colorBackground, overlay.CustomBackground);
                Tint(_colorText, overlay.CustomText);
                Tint(_colorAccent, overlay.CustomAccent);
                if (_theme.Enabled) _customColors.Enabled = overlay.Theme == OverlaySettings.CustomTheme;
                _clock.Checked = overlay.Clock;
                _headsetInfo.Checked = overlay.HeadsetInfo;
                _panelSession.Checked = overlay.PanelSession;
                _panelBattery.Checked = overlay.PanelBattery;
                _gpuTiming.Checked = overlay.GpuTiming;
                _reports.Checked = overlay.ReportsEnabled;
                _reportHardware.Checked = overlay.ReportHardware;
                _openVr.Checked = overlay.OpenVR;
                _anchor.SelectedIndex = overlay.Anchor;
                _handPlacement.SelectedIndex = overlay.HandPlacement;
                _gazeOnly.Checked = overlay.GazeOnly;
                SetSlider(_handAlong, overlay.HandAlong * 100);
                SetSlider(_handLift, overlay.HandLift * 100);
                _watchEnabled.Checked = overlay.WatchEnabled;
                _watchAnchor.SelectedIndex = overlay.WatchAnchor;
                _watchPlacement.SelectedIndex = overlay.WatchPlacement;
                _watchStyle.SelectedIndex = overlay.WatchStyle;
                _watchSession.Checked = overlay.WatchSession;
                _watchBattery.Checked = overlay.WatchBattery;
                _watchFps.Checked = overlay.WatchFps;
                _watchGazeOnly.Checked = overlay.WatchGazeOnly;
                SetSlider(_watchSize, overlay.WatchSize * 100);
                SetSlider(_watchOpacity, overlay.WatchOpacity * 100);
                SetSlider(_watchAlong, overlay.WatchAlong * 100);
                SetSlider(_watchLift, overlay.WatchLift * 100);
                _windowEnabled.Checked = overlay.WindowEnabled;
                _windowAnchor.SelectedIndex = overlay.WindowAnchor;
                _windowPlacement.SelectedIndex = overlay.WindowPlacement;
                _windowGazeOnly.Checked = overlay.WindowGazeOnly;
                SetSlider(_windowAlong, overlay.WindowAlong * 100);
                SetSlider(_windowLift, overlay.WindowLift * 100);
                SetSlider(_windowWidth, overlay.WindowWidth * 100);
                SetSlider(_windowDistance, overlay.WindowDistance * 100);
                SetSlider(_captureOpacity, _desktop.CaptureOpacity);
                SetSlider(_captureFps, _desktop.CaptureFps);
                _cropLeft.Value = Math.Min(Settings.MaxCrop, Math.Max(0, _desktop.CropLeft));
                _cropTop.Value = Math.Min(Settings.MaxCrop, Math.Max(0, _desktop.CropTop));
                _cropRight.Value = Math.Min(Settings.MaxCrop, Math.Max(0, _desktop.CropRight));
                _cropBottom.Value = Math.Min(Settings.MaxCrop, Math.Max(0, _desktop.CropBottom));
                UpdateHandControls();
                RefreshWatchPreview();
                LoadDesktop();
            }
            finally
            {
                _loading = false;
            }
        }

        /// <summary>Reflète les réglages du bureau quand ils changent ailleurs (menu de l'icône, fenêtre miroir fermée ou déplacée).</summary>
        public void LoadDesktop()
        {
            bool wasLoading = _loading;
            _loading = true;
            try
            {
                _desktopShown.Checked = _desktop.DesktopPanel;
                _desktopAnchor.SelectedIndex = Math.Min(AnchorLabels.Length - 1, Math.Max(0, _desktop.Anchor));
                SetSlider(_desktopScale, _desktop.Scale);
                SetSlider(_desktopOpacity, _desktop.Opacity);
                _desktopTopMost.Checked = _desktop.TopMost;
            }
            finally
            {
                _loading = wasLoading;
            }
        }

        // Fermer la fenêtre la masque seulement : le programme continue dans la zone de notification.
        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _watchTimer.Dispose();
                _windowTimer.Dispose();
                if (_watchPreview != null && _watchPreview.Image != null) _watchPreview.Image.Dispose();
                if (_windowPreview != null && _windowPreview.Image != null) _windowPreview.Image.Dispose();
            }
            base.Dispose(disposing);
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            if (e.CloseReason == CloseReason.UserClosing)
            {
                e.Cancel = true;
                Hide();
            }
            base.OnFormClosing(e);
        }
    }
}
