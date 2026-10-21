// Réglages échangés avec l'interface (ui/app.js) : écriture en JSON, et application d'un réglage changé
// dans la page, avec les mêmes bornes que la layer (layer/src/config.h) et que le .ini.

using System;
using System.Drawing;
using System.Globalization;
using System.Linq;
using System.Text.Json;

namespace XRPerfSensors
{
    internal static class SettingsJson
    {
        private static string Hex(Color c)
        {
            return string.Format(CultureInfo.InvariantCulture, "#{0:X2}{1:X2}{2:X2}", c.R, c.G, c.B);
        }

        private static void F(Utf8JsonWriter w, string name, float value)
        {
            w.WriteNumber(name, Math.Round(value, 3));
        }

        public static void WriteOverlay(Utf8JsonWriter w, OverlaySettings o)
        {
            w.WriteStartObject();
            w.WriteBoolean("Visible", o.Visible);
            w.WriteNumber("Position", o.Position);
            F(w, "Distance", o.Distance); F(w, "OffsetX", o.OffsetX); F(w, "OffsetY", o.OffsetY);
            F(w, "Width", o.Width); F(w, "Opacity", o.Opacity); F(w, "GlobalOpacity", o.GlobalOpacity);
            w.WriteBoolean("FaceViewer", o.FaceViewer);
            w.WriteBoolean("Clock", o.Clock);
            w.WriteBoolean("HeadsetInfo", o.HeadsetInfo);
            w.WriteBoolean("PanelSession", o.PanelSession);
            w.WriteBoolean("PanelBattery", o.PanelBattery);
            w.WriteNumber("Layout", o.Layout);
            w.WriteNumber("Theme", o.Theme);
            w.WriteString("CustomBackground", Hex(o.CustomBackground));
            w.WriteString("CustomText", Hex(o.CustomText));
            w.WriteString("CustomAccent", Hex(o.CustomAccent));
            w.WriteString("HeadsetName", o.HeadsetName ?? "");
            w.WriteNumber("Anchor", o.Anchor);
            w.WriteNumber("HandPlacement", o.HandPlacement);
            w.WriteBoolean("GazeOnly", o.GazeOnly);
            F(w, "HandAlong", o.HandAlong); F(w, "HandLift", o.HandLift);
            w.WriteBoolean("WatchEnabled", o.WatchEnabled);
            w.WriteNumber("WatchAnchor", o.WatchAnchor);
            w.WriteNumber("WatchPlacement", o.WatchPlacement);
            w.WriteNumber("WatchStyle", o.WatchStyle);
            w.WriteBoolean("WatchSession", o.WatchSession);
            w.WriteBoolean("WatchBattery", o.WatchBattery);
            w.WriteBoolean("WatchFps", o.WatchFps);
            w.WriteBoolean("WatchGazeOnly", o.WatchGazeOnly);
            F(w, "WatchSize", o.WatchSize); F(w, "WatchOpacity", o.WatchOpacity); F(w, "WatchAlong", o.WatchAlong); F(w, "WatchLift", o.WatchLift);
            w.WriteBoolean("WindowEnabled", o.WindowEnabled);
            w.WriteNumber("WindowAnchor", o.WindowAnchor);
            w.WriteNumber("WindowPlacement", o.WindowPlacement);
            w.WriteBoolean("WindowGazeOnly", o.WindowGazeOnly);
            F(w, "WindowWidth", o.WindowWidth); F(w, "WindowAlong", o.WindowAlong); F(w, "WindowLift", o.WindowLift); F(w, "WindowDistance", o.WindowDistance);
            w.WriteBoolean("GpuTiming", o.GpuTiming);
            w.WriteString("ReportMode", OverlaySettings.ReportModeNames[Math.Min(2, Math.Max(0, o.ReportMode))]);
            w.WriteBoolean("ReportHardware", o.ReportHardware);
            w.WriteBoolean("OpenVR", o.OpenVR);
            w.WriteEndObject();
        }

        public static void WriteDesktop(Utf8JsonWriter w, Settings d)
        {
            w.WriteStartObject();
            w.WriteBoolean("DesktopPanel", d.DesktopPanel);
            w.WriteBoolean("TopMost", d.TopMost);
            w.WriteNumber("Scale", d.Scale);
            w.WriteNumber("Opacity", d.Opacity);
            w.WriteNumber("Anchor", d.Anchor);
            w.WriteNumber("CaptureFps", d.CaptureFps);
            w.WriteNumber("CaptureOpacity", d.CaptureOpacity);
            w.WriteNumber("CropLeft", d.CropLeft);
            w.WriteNumber("CropTop", d.CropTop);
            w.WriteNumber("CropRight", d.CropRight);
            w.WriteNumber("CropBottom", d.CropBottom);
            w.WriteString("Language", d.Language);
            w.WriteString("UiTheme", d.UiTheme);
            w.WriteString("UiBackground", d.UiBackground);
            w.WriteString("UiText", d.UiText);
            w.WriteString("UiAccent", d.UiAccent);
            w.WriteNumber("UiScale", d.UiScale);
            w.WriteBoolean("UiAnimations", d.UiAnimations);
            w.WriteBoolean("UiStartFullscreen", d.UiStartFullscreen);
            w.WriteEndObject();
        }

        private static bool Bool(JsonElement v, out bool value)
        {
            value = v.ValueKind == JsonValueKind.True;
            return v.ValueKind == JsonValueKind.True || v.ValueKind == JsonValueKind.False;
        }

        private static bool Int(JsonElement v, int min, int max, out int value)
        {
            value = 0;
            if (v.ValueKind != JsonValueKind.Number || !v.TryGetDouble(out double d) || double.IsNaN(d)) return false;
            value = (int)Math.Round(Math.Min(max, Math.Max(min, d)));
            return true;
        }

        private static bool Float(JsonElement v, float min, float max, out float value)
        {
            value = 0f;
            if (v.ValueKind != JsonValueKind.Number || !v.TryGetDouble(out double d) || double.IsNaN(d)) return false;
            value = (float)Math.Min(max, Math.Max(min, d));
            return true;
        }

        private static bool ColorOf(JsonElement v, out Color value)
        {
            value = Color.Black;
            string text = v.ValueKind == JsonValueKind.String ? v.GetString() : null;
            if (!Settings.IsColor(text)) return false;
            int rgb = int.Parse(text.Substring(1), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            value = Color.FromArgb((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
            return true;
        }

        /// <summary>Applique un réglage du casque. Retourne false si la clé ou la valeur ne convient pas.</summary>
        public static bool ApplyOverlay(OverlaySettings o, string key, JsonElement v)
        {
            bool b; int i; float f; Color c;
            switch (key)
            {
                case "Visible": if (!Bool(v, out b)) return false; o.Visible = b; return true;
                case "Position": if (!Int(v, 0, OverlaySettings.PositionNames.Length - 1, out i)) return false; o.Position = i; return true;
                case "Distance": if (!Float(v, 0.3f, 5f, out f)) return false; o.Distance = f; return true;
                case "OffsetX": if (!Float(v, 0f, 3f, out f)) return false; o.OffsetX = f; return true;
                case "OffsetY": if (!Float(v, 0f, 3f, out f)) return false; o.OffsetY = f; return true;
                case "Width": if (!Float(v, 0.10f, 1.50f, out f)) return false; o.Width = f; return true;
                case "Opacity": if (!Float(v, 0f, 1f, out f)) return false; o.Opacity = f; return true;
                case "GlobalOpacity": if (!Float(v, 0.20f, 1f, out f)) return false; o.GlobalOpacity = f; return true;
                case "FaceViewer": if (!Bool(v, out b)) return false; o.FaceViewer = b; return true;
                case "Clock": if (!Bool(v, out b)) return false; o.Clock = b; return true;
                case "HeadsetInfo": if (!Bool(v, out b)) return false; o.HeadsetInfo = b; return true;
                case "HeadsetName":
                    // Texte simple (le panneau du casque n'affiche que l'ASCII) : sans retour à la ligne ni « ; »
                    // ou « [ » qui gêneraient le .ini, 40 caractères au plus.
                    if (v.ValueKind != JsonValueKind.String) return false;
                    string name = (v.GetString() ?? "").Trim();
                    if (name.Length > 40 || name.Any(ch => ch < 0x20 || ch > 0x7E || ch == ';' || ch == '[' || ch == ']')) return false;
                    o.HeadsetName = name;
                    return true;
                case "PanelSession": if (!Bool(v, out b)) return false; o.PanelSession = b; return true;
                case "PanelBattery": if (!Bool(v, out b)) return false; o.PanelBattery = b; return true;
                case "Layout": if (!Int(v, 0, OverlaySettings.LayoutNames.Length - 1, out i)) return false; o.Layout = i; return true;
                case "Theme": if (!Int(v, 0, OverlaySettings.ThemeNames.Length - 1, out i)) return false; o.Theme = i; return true;
                case "CustomBackground": if (!ColorOf(v, out c)) return false; o.CustomBackground = c; return true;
                case "CustomText": if (!ColorOf(v, out c)) return false; o.CustomText = c; return true;
                case "CustomAccent": if (!ColorOf(v, out c)) return false; o.CustomAccent = c; return true;
                case "Anchor": if (!Int(v, 0, OverlaySettings.AnchorNames.Length - 1, out i)) return false; o.Anchor = i; return true;
                case "HandPlacement": if (!Int(v, 0, OverlaySettings.PlacementNames.Length - 1, out i)) return false; o.HandPlacement = i; return true;
                case "GazeOnly": if (!Bool(v, out b)) return false; o.GazeOnly = b; return true;
                case "HandAlong": if (!Float(v, OverlaySettings.MinHandAlong, OverlaySettings.MaxHandAlong, out f)) return false; o.HandAlong = f; return true;
                case "HandLift": if (!Float(v, OverlaySettings.MinHandLift, OverlaySettings.MaxHandLift, out f)) return false; o.HandLift = f; return true;
                case "WatchEnabled": if (!Bool(v, out b)) return false; o.WatchEnabled = b; return true;
                case "WatchAnchor": if (!Int(v, 0, OverlaySettings.AnchorNames.Length - 1, out i)) return false; o.WatchAnchor = i; return true;
                case "WatchPlacement": if (!Int(v, 0, OverlaySettings.PlacementNames.Length - 1, out i)) return false; o.WatchPlacement = i; return true;
                case "WatchStyle": if (!Int(v, 0, OverlaySettings.WatchStyleNames.Length - 1, out i)) return false; o.WatchStyle = i; return true;
                case "WatchSession": if (!Bool(v, out b)) return false; o.WatchSession = b; return true;
                case "WatchBattery": if (!Bool(v, out b)) return false; o.WatchBattery = b; return true;
                case "WatchFps": if (!Bool(v, out b)) return false; o.WatchFps = b; return true;
                case "WatchGazeOnly": if (!Bool(v, out b)) return false; o.WatchGazeOnly = b; return true;
                case "WatchSize": if (!Float(v, OverlaySettings.MinWatchSize, OverlaySettings.MaxWatchSize, out f)) return false; o.WatchSize = f; return true;
                case "WatchOpacity": if (!Float(v, OverlaySettings.MinWatchOpacity, 1f, out f)) return false; o.WatchOpacity = f; return true;
                case "WatchAlong": if (!Float(v, OverlaySettings.MinHandAlong, OverlaySettings.MaxHandAlong, out f)) return false; o.WatchAlong = f; return true;
                case "WatchLift": if (!Float(v, OverlaySettings.MinHandLift, OverlaySettings.MaxHandLift, out f)) return false; o.WatchLift = f; return true;
                case "WindowEnabled": if (!Bool(v, out b)) return false; o.WindowEnabled = b; return true;
                case "WindowAnchor": if (!Int(v, 0, OverlaySettings.WindowAnchorNames.Length - 1, out i)) return false; o.WindowAnchor = i; return true;
                case "WindowPlacement": if (!Int(v, 0, OverlaySettings.PlacementNames.Length - 1, out i)) return false; o.WindowPlacement = i; return true;
                case "WindowGazeOnly": if (!Bool(v, out b)) return false; o.WindowGazeOnly = b; return true;
                case "WindowWidth": if (!Float(v, OverlaySettings.MinWindowWidth, OverlaySettings.MaxWindowWidth, out f)) return false; o.WindowWidth = f; return true;
                case "WindowAlong": if (!Float(v, OverlaySettings.MinHandAlong, OverlaySettings.MaxHandAlong, out f)) return false; o.WindowAlong = f; return true;
                case "WindowLift": if (!Float(v, OverlaySettings.MinHandLift, OverlaySettings.MaxHandLift, out f)) return false; o.WindowLift = f; return true;
                case "WindowDistance": if (!Float(v, OverlaySettings.MinWindowDistance, OverlaySettings.MaxWindowDistance, out f)) return false; o.WindowDistance = f; return true;
                case "GpuTiming": if (!Bool(v, out b)) return false; o.GpuTiming = b; return true;
                case "ReportMode":
                    {
                        int mode = v.ValueKind == JsonValueKind.String ? Array.IndexOf(OverlaySettings.ReportModeNames, v.GetString()) : -1;
                        if (mode < 0 || mode == o.ReportMode) return false;
                        o.ReportMode = mode;
                        return true;
                    }
                case "ReportHardware": if (!Bool(v, out b)) return false; o.ReportHardware = b; return true;
                case "OpenVR": if (!Bool(v, out b)) return false; o.OpenVR = b; return true;
            }
            return false;
        }

        /// <summary>Ce qu'un réglage du bureau change, pour savoir quoi mettre à jour.</summary>
        public enum Change { None, Mirror, Capture, Interface, Language }

        /// <summary>Applique un réglage du bureau (fenêtre miroir, capture, interface, langue).</summary>
        public static Change ApplyDesktop(Settings d, string key, JsonElement v)
        {
            bool b; int i;
            string text = v.ValueKind == JsonValueKind.String ? v.GetString() : null;
            switch (key)
            {
                case "DesktopPanel": if (!Bool(v, out b)) return Change.None; d.DesktopPanel = b; return Change.Mirror;
                case "TopMost": if (!Bool(v, out b)) return Change.None; d.TopMost = b; return Change.Mirror;
                case "Scale": if (!Int(v, Settings.MinScale, Settings.MaxScale, out i)) return Change.None; d.Scale = i; return Change.Mirror;
                case "Opacity": if (!Int(v, Settings.MinOpacity, 100, out i)) return Change.None; d.Opacity = i; return Change.Mirror;
                case "Anchor": if (!Int(v, 0, 6, out i)) return Change.None; d.Anchor = i; return Change.Mirror;
                case "CaptureFps": if (!Int(v, Settings.MinCaptureFps, Settings.MaxCaptureFps, out i)) return Change.None; d.CaptureFps = i; return Change.Capture;
                case "CaptureOpacity": if (!Int(v, Settings.MinOpacity, 100, out i)) return Change.None; d.CaptureOpacity = i; return Change.Capture;
                case "CropLeft": if (!Int(v, 0, Settings.MaxCrop, out i)) return Change.None; d.CropLeft = i; return Change.Capture;
                case "CropTop": if (!Int(v, 0, Settings.MaxCrop, out i)) return Change.None; d.CropTop = i; return Change.Capture;
                case "CropRight": if (!Int(v, 0, Settings.MaxCrop, out i)) return Change.None; d.CropRight = i; return Change.Capture;
                case "CropBottom": if (!Int(v, 0, Settings.MaxCrop, out i)) return Change.None; d.CropBottom = i; return Change.Capture;
                case "Language": if (Array.IndexOf(Loc.Names, text) < 0) return Change.None; d.Language = text; return Change.Language;
                case "UiTheme": if (Array.IndexOf(Settings.UiThemes, text) < 0) return Change.None; d.UiTheme = text; return Change.Interface;
                case "UiBackground": if (!Settings.IsColor(text)) return Change.None; d.UiBackground = text; return Change.Interface;
                case "UiText": if (!Settings.IsColor(text)) return Change.None; d.UiText = text; return Change.Interface;
                case "UiAccent": if (!Settings.IsColor(text)) return Change.None; d.UiAccent = text; return Change.Interface;
                case "UiScale": if (!Int(v, Settings.MinUiScale, Settings.MaxUiScale, out i)) return Change.None; d.UiScale = i; return Change.Interface;
                case "UiAnimations": if (!Bool(v, out b)) return Change.None; d.UiAnimations = b; return Change.Interface;
                case "UiStartFullscreen": if (!Bool(v, out b)) return Change.None; d.UiStartFullscreen = b; return Change.Interface;
            }
            return Change.None;
        }
    }
}
