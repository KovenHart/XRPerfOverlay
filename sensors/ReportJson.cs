// Rapports de session mis en forme pour l'interface (ui/app.js) : liste, et détail d'un rapport avec
// ses analyses dans la langue choisie, ses chiffres et son relevé minute par minute. Sans Windows Forms :
// ce fichier est aussi exécuté par les essais hors Windows.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.Json;

namespace XRPerfSensors
{
    internal static class ReportJson
    {
        /// <summary>Date du rapport au format ISO (heure locale), lisible par JavaScript.</summary>
        public static string Iso(SessionReport r)
        {
            return (r.Start ?? r.FileTime).ToString("yyyy-MM-dd'T'HH:mm:ss", CultureInfo.InvariantCulture);
        }

        private static void Num(Utf8JsonWriter w, string name, double value)
        {
            if (double.IsNaN(value) || double.IsInfinity(value)) w.WriteNull(name);
            else w.WriteNumber(name, Math.Round(value, 3));
        }

        private static void Values(Utf8JsonWriter w, string name, IEnumerable<double> values)
        {
            w.WriteStartArray(name);
            foreach (double v in values)
            {
                if (double.IsNaN(v) || double.IsInfinity(v)) w.WriteNullValue();
                else w.WriteNumberValue(Math.Round(v, 3));
            }
            w.WriteEndArray();
        }

        /// <summary>Liste des rapports (le plus récent d'abord) ; <paramref name="previousOf"/> donne la session précédente du même jeu.</summary>
        public static string List(IList<SessionReport> reports, IList<string> folders, Func<SessionReport, SessionReport> previousOf, bool english)
        {
            using (var stream = new MemoryStream())
            {
                using (var w = new Utf8JsonWriter(stream))
                {
                    w.WriteStartObject();
                    w.WriteString("type", "reports");
                    w.WriteStartArray("folders");
                    foreach (string folder in folders) w.WriteStringValue(folder);
                    w.WriteEndArray();
                    w.WriteStartArray("items");
                    foreach (SessionReport r in reports)
                    {
                        NoviceView view = NoviceSummary.Build(r, previousOf(r), english);
                        w.WriteStartObject();
                        w.WriteString("id", r.Path);
                        w.WriteString("game", r.Game);
                        w.WriteString("start", Iso(r));
                        w.WriteString("duration", r.DurationShort == "--" ? "" : r.DurationShort);
                        Num(w, "fpsAvg", r.FpsAvg);
                        Num(w, "low1", r.Low1);
                        Num(w, "targetHeld", r.TargetHeld);
                        w.WriteBoolean("finished", r.Finished);
                        w.WriteBoolean("manual", r.Manual);
                        w.WriteBoolean("readable", r.Readable);
                        w.WriteString("level", view.Level.ToString());
                        w.WriteString("verdict", view.Verdict);
                        w.WriteEndObject();
                    }
                    w.WriteEndArray();
                    w.WriteEndObject();
                }
                return Encoding.UTF8.GetString(stream.ToArray());
            }
        }

        /// <summary>Contexte de la session, en courtes étiquettes : casque, runtime, rendu, API, matériel.</summary>
        private static List<string> Chips(SessionReport r, bool english)
        {
            var chips = new List<string>();
            if (r.Headset.Length > 0 && r.Headset != "--") chips.Add(r.Headset);
            if (r.Runtime.Length > 0 && r.Runtime != "--") chips.Add(r.Runtime);
            if (r.Render.Length > 0 && r.Render != "--")
            {
                double scale = ReportAnalysis.RenderScale(r);
                string text = (english ? "render " : "rendu ") + r.Render + (english ? " per eye" : " par œil");
                if (!double.IsNaN(scale) && Math.Abs(scale - 1.0) >= 0.03)
                    text += string.Format(CultureInfo.InvariantCulture, english ? " ({0} % of {1})" : " ({0} % de {1})",
                        (100.0 * scale).ToString("0", CultureInfo.InvariantCulture), r.RenderRecommended);
                chips.Add(text);
            }
            else if (r.OpenVR && r.RenderRecommended.Length > 0)
            {
                // Jeu SteamVR : la taille de l'image du jeu n'est pas visible de l'extérieur ; celle que SteamVR
                // recommande (son réglage de résolution), que les jeux suivent en général.
                chips.Add((english ? "recommended render " : "rendu recommandé ") + r.RenderRecommended + (english ? " per eye" : " par œil"));
            }
            // Jeu SteamVR : l'API graphique n'est pas relevée, l'étiquette n'apprendrait rien.
            if (r.Api.Length > 0 && !r.OpenVR) chips.Add(r.Api);
            if (r.Cpu.Length > 0) chips.Add(r.Cpu);
            if (r.Gpu.Length > 0) chips.Add(r.Gpu);
            return chips;
        }

        /// <summary>Un rapport en détail (objet JSON écrit dans <paramref name="w"/>).</summary>
        public static void Detail(Utf8JsonWriter w, SessionReport r, SessionReport previous, bool english)
        {
            w.WriteStartObject();
            w.WriteString("id", r.Path);
            w.WriteString("game", r.Game);
            w.WriteString("start", Iso(r));
            w.WriteString("duration", r.DurationShort == "--" ? "" : r.DurationShort);
            w.WriteBoolean("finished", r.Finished);
            w.WriteBoolean("manual", r.Manual);
            w.WriteBoolean("readable", r.Readable);
            w.WriteBoolean("gpuImplausible", r.GpuMeasureImplausible);
            w.WriteBoolean("minutesGpuMeasured", r.MinutesGpuMeasured);
            if (previous != null) w.WriteString("previousId", previous.Path);

            w.WriteStartArray("chips");
            foreach (string chip in Chips(r, english)) w.WriteStringValue(chip);
            w.WriteEndArray();

            w.WriteStartObject("n");
            Num(w, "fpsAvg", r.FpsAvg); Num(w, "fpsMin", r.FpsMin); Num(w, "fpsMax", r.FpsMax);
            Num(w, "low1", r.Low1); Num(w, "low01", r.Low01);
            Num(w, "target", r.Target); Num(w, "targetHeld", r.TargetHeld); Num(w, "outFps", r.OutFps);
            Num(w, "frames", r.Frames); Num(w, "pauses", r.Pauses);
            Num(w, "frameAvg", r.FrameAvg); Num(w, "frameMax", r.FrameMax);
            Num(w, "cpuMsAvg", r.CpuMsAvg); Num(w, "cpuMsMax", r.CpuMsMax);
            w.WriteBoolean("gpuIsMeasured", r.GpuIsMeasured);
            Num(w, "gpuMsAvg", r.GpuMsAvg);
            Num(w, "gpuMsMax", r.GpuIsMeasured ? r.GpuMeasuredMax : r.GpuEstimateMax);
            Num(w, "gpuEstimateAvg", r.GpuEstimateAvg); Num(w, "gpuEstimateMax", r.GpuEstimateMax);
            Num(w, "cpuLoadAvg", r.CpuLoadAvg); Num(w, "cpuLoadMax", r.CpuLoadMax);
            Num(w, "cpuTempAvg", r.CpuTempAvg); Num(w, "cpuTempMax", r.CpuTempMax); Num(w, "cpuMHzAvg", r.CpuMHzAvg);
            Num(w, "gpuLoadAvg", r.GpuLoadAvg); Num(w, "gpuLoadMax", r.GpuLoadMax);
            Num(w, "gpuTempAvg", r.GpuTempAvg); Num(w, "gpuTempMax", r.GpuTempMax); Num(w, "gpuMHzAvg", r.GpuMHzAvg);
            Num(w, "vramAvg", r.VramAvg); Num(w, "vramMax", r.VramMax); Num(w, "vramTotal", r.VramTotal);
            Num(w, "budgetMs", !double.IsNaN(r.BudgetMs) ? r.BudgetMs : r.Target > 0 ? 1000.0 / r.Target : double.NaN);
            Values(w, "distribution", r.Distribution);
            w.WriteEndObject();

            if (r.Minutes.Count >= 2)
            {
                w.WriteStartObject("minutes");
                var minute = new List<double>();
                foreach (MinuteRow m in r.Minutes) minute.Add(m.Minute);
                Values(w, "minute", minute);
                Values(w, "fpsAvg", r.Minutes.ConvertAll(m => m.FpsAvg));
                Values(w, "fpsMin", r.Minutes.ConvertAll(m => m.FpsMin));
                Values(w, "cpuMs", r.Minutes.ConvertAll(m => m.CpuMs));
                Values(w, "gpuMs", r.Minutes.ConvertAll(m => m.GpuMs));
                Values(w, "cpuLoad", r.Minutes.ConvertAll(m => m.CpuLoad));
                Values(w, "gpuLoad", r.Minutes.ConvertAll(m => m.GpuLoad));
                Values(w, "cpuTemp", r.Minutes.ConvertAll(m => m.CpuTemp));
                Values(w, "gpuTemp", r.Minutes.ConvertAll(m => m.GpuTemp));
                Values(w, "vram", r.Minutes.ConvertAll(m => m.VramGb));
                w.WriteEndObject();
            }

            w.WriteStartArray("findings");
            foreach (Finding f in ReportAnalysis.Analyze(r, previous, english))
            {
                w.WriteStartObject();
                w.WriteString("level", f.Level.ToString());
                w.WriteString("text", f.Text);
                w.WriteEndObject();
            }
            w.WriteEndArray();

            NoviceView view = NoviceSummary.Build(r, previous, english);
            w.WriteStartObject("simple");
            w.WriteString("level", view.Level.ToString());
            w.WriteString("verdict", view.Verdict);
            w.WriteString("summary", view.Summary);
            w.WriteString("limiter", view.Limiter);
            w.WriteStartArray("sections");
            foreach (NoviceSection section in view.Sections)
            {
                w.WriteStartObject();
                w.WriteString("kind", section.Kind);
                w.WriteString("title", section.Title);
                w.WriteStartArray("lines");
                foreach (string line in section.Lines) w.WriteStringValue(line);
                w.WriteEndArray();
                w.WriteEndObject();
            }
            w.WriteEndArray();
            w.WriteStartArray("tips");
            foreach (string tip in view.Tips) w.WriteStringValue(tip);
            w.WriteEndArray();
            w.WriteStartArray("glossary");
            foreach (string[] entry in english ? NoviceSummary.GlossaryEnglish : NoviceSummary.Glossary)
            {
                w.WriteStartArray();
                w.WriteStringValue(entry[0]);
                w.WriteStringValue(entry[1]);
                w.WriteEndArray();
            }
            w.WriteEndArray();
            if (previous != null && previous.Readable && !double.IsNaN(previous.FpsAvg))
            {
                w.WriteStartObject("previous");
                Num(w, "fpsAvg", previous.FpsAvg);
                Num(w, "targetHeld", previous.TargetHeld);
                w.WriteString("start", Iso(previous));
                w.WriteEndObject();
            }
            w.WriteEndObject();

            w.WriteString("raw", r.Text.TrimStart('﻿'));
            w.WriteEndObject();
        }

        /// <summary>
        /// Message « report » : le rapport choisi, et celui auquel il est comparé (le second choisi, sinon la
        /// session précédente du même jeu), avec les différences de réglages et de matériel.
        /// </summary>
        public static string Message(SessionReport report, SessionReport previousOfReport, SessionReport other, SessionReport previousOfOther, bool chosen, bool english)
        {
            using (var stream = new MemoryStream())
            {
                using (var w = new Utf8JsonWriter(stream))
                {
                    w.WriteStartObject();
                    w.WriteString("type", "report");
                    if (report != null)
                    {
                        w.WritePropertyName("report");
                        Detail(w, report, previousOfReport, english);
                    }
                    if (report != null && other != null)
                    {
                        w.WritePropertyName("other");
                        Detail(w, other, previousOfOther, english);
                        w.WriteBoolean("chosen", chosen);
                        w.WriteString("setup", ReportAnalysis.SetupDifference(report, other, english));
                        w.WriteString("hardware", ReportAnalysis.HardwareDifference(report, other, english));
                    }
                    w.WriteEndObject();
                }
                return Encoding.UTF8.GetString(stream.ToArray());
            }
        }
    }
}
