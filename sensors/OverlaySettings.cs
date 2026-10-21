// Réglages de l'overlay depuis XRPerfSensors : modèle (lu et écrit dans XRPerfOverlay.ini), envoi
// en direct à la layer par une mémoire partagée, et aperçu hors jeu dessiné par la layer.

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
        // Mode des rapports ([summary] mode), dans l'ordre de ReportMode de layer/src/config.h.
        public static readonly string[] ReportModeNames = { "auto", "manual", "off" };
        public const int ReportAuto = 0, ReportManual = 1, ReportOff = 2;
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
        /// <summary>Langue du panneau, de la montre et des rapports : 0 = celle de Windows, 1 = français, 2 = anglais (Loc.Names).</summary>
        public int Language;

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
        public int ReportMode = ReportAuto; // [summary] mode : rapport par session, entre deux Ctrl+Alt+S, ou aucun (changeable en jeu)
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
            s.Language = ReadName(path, "language", Loc.Names, s.Language);
            s.GpuTiming = ReadBool(path, "gpu", "measure", s.GpuTiming);
            // Sans "mode" (fichier d'une version précédente) : "enabled=0" veut dire aucun rapport.
            // Valeur inconnue : même repli que la layer (config.cpp).
            int reportMode = Array.IndexOf(ReportModeNames, ReadText(path, "summary", "mode").Trim().ToLowerInvariant());
            s.ReportMode = reportMode >= 0 ? reportMode : ReadBool(path, "summary", "enabled", true) ? ReportAuto : ReportOff;
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
            // Nom du casque modifié à la main dans le .ini pendant que le programme tourne : repris aussi.
            if (HeadsetName == baseline.HeadsetName && HeadsetName != fresh.HeadsetName) { HeadsetName = fresh.HeadsetName; changed = true; }
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
            // Vide : la clé est retirée, la layer reprend le nom annoncé par le runtime (retirer une clé absente
            // n'est pas un échec : le résultat n'est pas compté).
            if (string.IsNullOrEmpty(HeadsetName)) WritePrivateProfileStringW(Section, "headset_name", null, path);
            else ok &= WritePrivateProfileStringW(Section, "headset_name", HeadsetName, path);
            ok &= WritePrivateProfileStringW(Section, "session", PanelSession ? "1" : "0", path);
            ok &= WritePrivateProfileStringW(Section, "battery", PanelBattery ? "1" : "0", path);
            ok &= WritePrivateProfileStringW(Section, "layout", LayoutNames[Layout], path);
            ok &= WritePrivateProfileStringW(Section, "theme", ThemeNames[Theme], path);
            ok &= WritePrivateProfileStringW(Section, "custom_background", Hex(CustomBackground), path);
            ok &= WritePrivateProfileStringW(Section, "custom_text", Hex(CustomText), path);
            ok &= WritePrivateProfileStringW(Section, "custom_accent", Hex(CustomAccent), path);
            ok &= WritePrivateProfileStringW(Section, "language", Loc.Names[Math.Min(Loc.Names.Length - 1, Math.Max(0, Language))], path);
            ok &= WritePrivateProfileStringW("gpu", "measure", GpuTiming ? "1" : "0", path);
            ok &= WritePrivateProfileStringW("summary", "mode", ReportModeNames[Math.Min(ReportModeNames.Length - 1, Math.Max(0, ReportMode))], path);
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
        /// layer/src/shared_block.h : mêmes offsets, 192 octets).
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
            // Langue effective (1 = français, 2 = anglais) : « auto » est résolu ici, sur le même PC que le jeu.
            Put(block, 184, Language == 1 ? 1u : Language == 2 ? 2u : Loc.Resolve("auto") == "en" ? 2u : 1u);
            // Mode des rapports : 1 = automatique, 2 = manuel, 3 = aucun (0 laisserait celui du .ini).
            Put(block, 188, (uint)(Math.Min(ReportOff, Math.Max(ReportAuto, ReportMode)) + 1));
            return block;
        }
    }

    /// <summary>
    /// Mémoire partagée "Local\XRPerfOverlay.Settings" : les réglages en cours, que la layer relit
    /// quelques fois par seconde pour les appliquer dans un jeu déjà lancé.
    /// </summary>
    internal sealed class SettingsShare : IDisposable
    {
        public const int Size = 192;
        public const uint Magic = 0x53505258; // "XRPS"
        public const uint Version = 5;

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
                string name = string.IsNullOrEmpty(settings.HeadsetName) ? Loc.T("Nom du casque", "Headset name") : settings.HeadsetName;
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
}
