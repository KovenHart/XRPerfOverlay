// Lecture des rapports de session (.txt écrits par la layer, voir layer/src/session_stats.h) et
// analyse de leurs valeurs. Aucune dépendance à Windows Forms : ce fichier est aussi exécuté tel
// quel par les essais hors Windows.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

namespace XRPerfSensors
{
    /// <summary>Une ligne du relevé minute par minute.</summary>
    internal sealed class MinuteRow
    {
        public int Minute;
        public double FpsAvg = double.NaN, FpsMin = double.NaN, CpuMs = double.NaN, GpuMs = double.NaN;
        public double CpuLoad = double.NaN, GpuLoad = double.NaN, CpuTemp = double.NaN, GpuTemp = double.NaN, VramGb = double.NaN;
        /// <summary>Temps GPU de cette minute estimé faute de mesure (affiché "~" dans le rapport).</summary>
        public bool GpuEstimated;
    }

    /// <summary>Contenu d'un rapport de session. NaN = valeur absente ou affichée "--".</summary>
    internal sealed class SessionReport
    {
        public string Path = "";
        public string Text = "";
        public DateTime FileTime;
        public bool Readable;
        /// <summary>Fichier impossible à ouvrir ou à lire (supprimé ou verrouillé entre-temps).</summary>
        public bool LoadFailed;

        public string Game = "?", Api = "", Headset = "", Runtime = "", Render = "";
        /// <summary>Plus petite et plus grande résolution vues quand le jeu en a changé en cours de session (sinon vides).</summary>
        public string RenderMin = "", RenderMax = "";
        /// <summary>Résolution par œil recommandée par le runtime (vide : non indiquée, rapports d'avant la 1.2.2).</summary>
        public string RenderRecommended = "";
        /// <summary>Jeu SteamVR (OpenVR), mesuré par XRPerfVR et non par la layer.</summary>
        public bool OpenVR;
        /// <summary>Modèles du processeur et de la carte graphique (vide : non indiqués dans le rapport).</summary>
        public string Cpu = "", Gpu = "";
        public DateTime? Start;
        public bool Finished;
        public string EndText = "", DurationText = "";
        public double WallSeconds = double.NaN, ActiveSeconds = double.NaN;

        public double FpsAvg = double.NaN, FpsMin = double.NaN, FpsMax = double.NaN, Low1 = double.NaN, Low01 = double.NaN;
        public double Target = double.NaN, TargetHeld = double.NaN, OutFps = double.NaN, Frames = double.NaN, Pauses = double.NaN;
        public double FrameAvg = double.NaN, FrameMax = double.NaN, CpuMsAvg = double.NaN, CpuMsMax = double.NaN;
        public double GpuMeasuredAvg = double.NaN, GpuMeasuredMax = double.NaN, GpuEstimateAvg = double.NaN, GpuEstimateMax = double.NaN;
        public double CpuLoadAvg = double.NaN, CpuLoadMax = double.NaN, CpuTempAvg = double.NaN, CpuTempMax = double.NaN, CpuMHzAvg = double.NaN;
        public double GpuLoadAvg = double.NaN, GpuLoadMax = double.NaN, GpuTempAvg = double.NaN, GpuTempMax = double.NaN, GpuMHzAvg = double.NaN;
        public double VramAvg = double.NaN, VramMax = double.NaN, VramTotal = double.NaN;

        /// <summary>Répartition des temps d'image : dans le budget, jusqu'à 1,5 x, de 1,5 à 2 x, plus de 2 x (en %).</summary>
        public double[] Distribution = { double.NaN, double.NaN, double.NaN, double.NaN };
        public double BudgetMs = double.NaN;

        public readonly List<MinuteRow> Minutes = new List<MinuteRow>();
        public bool MinutesGpuMeasured;
        public bool SensorsMissing;
        /// <summary>La layer a écarté la mesure du temps GPU en cours de session (non représentative).</summary>
        public bool GpuMeasureRejected;

        /// <summary>
        /// Mesure du temps GPU incompatible avec la charge de la carte : bien plus basse que l'estimation alors
        /// que la carte travaille. Le rendu du jeu n'était pas encadré par les repères de la layer (rapports
        /// écrits avant que la layer ne sache l'écarter d'elle-même). Même règle que layer/src/overlay.cpp.
        /// </summary>
        public bool GpuMeasureImplausible
        {
            get { return !double.IsNaN(GpuMeasuredAvg) && GpuLoadAvg >= 50 && GpuEstimateAvg > 0 && GpuMeasuredAvg < 0.3 * GpuEstimateAvg; }
        }

        /// <summary>Temps GPU de référence : mesuré s'il existe, sinon estimé.</summary>
        public double GpuMsAvg
        {
            get { return GpuIsMeasured ? GpuMeasuredAvg : GpuEstimateAvg; }
        }

        /// <summary>Le rapport contient une mesure du temps GPU exploitable.</summary>
        public bool GpuIsMeasured
        {
            get { return !double.IsNaN(GpuMeasuredAvg) && !GpuMeasureImplausible; }
        }

        /// <summary>Durée de la session à afficher (temps écoulé).</summary>
        public string DurationShort
        {
            get
            {
                if (double.IsNaN(WallSeconds)) return "--";
                var span = TimeSpan.FromSeconds(WallSeconds);
                return span.TotalHours >= 1 ? string.Format(CultureInfo.InvariantCulture, "{0} h {1:00}", (int)span.TotalHours, span.Minutes)
                                            : string.Format(CultureInfo.InvariantCulture, "{0} min", Math.Max(1, (int)Math.Round(span.TotalMinutes)));
            }
        }

        // --- Lecture ---------------------------------------------------------------------------------

        private static readonly Regex Number = new Regex(@"--|-?\d+(?:\.\d+)?", RegexOptions.Compiled);

        /// <summary>Nombres d'une portion de ligne, dans l'ordre ("--" donne NaN).</summary>
        private static List<double> Numbers(string text)
        {
            var values = new List<double>();
            foreach (Match match in Number.Matches(text))
            {
                if (match.Value == "--") values.Add(double.NaN);
                else values.Add(double.Parse(match.Value, NumberStyles.Float, CultureInfo.InvariantCulture));
            }
            return values;
        }

        private static double At(List<double> values, int index)
        {
            return index < values.Count ? values[index] : double.NaN;
        }

        // "3 h 05 min 12 s", "25 min 12 s", "40 s" -> secondes.
        private static double Seconds(string text)
        {
            double total = 0;
            bool any = false;
            foreach (Match match in Regex.Matches(text, @"(\d+)\s*(h|min|s)\b"))
            {
                double value = double.Parse(match.Groups[1].Value, CultureInfo.InvariantCulture);
                total += match.Groups[2].Value == "h" ? value * 3600 : match.Groups[2].Value == "min" ? value * 60 : value;
                any = true;
            }
            return any ? total : double.NaN;
        }

        /// <summary>Ouvre sans gêner un jeu qui serait encore en train d'écrire ce rapport.</summary>
        private static string ReadShared(string path)
        {
            using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
            using (var reader = new StreamReader(stream, new UTF8Encoding(false), true))
            {
                return reader.ReadToEnd();
            }
        }

        public static SessionReport Load(string path)
        {
            var report = new SessionReport { Path = path };
            try
            {
                report.FileTime = File.GetLastWriteTime(path);
                report.Text = ReadShared(path);
                report.Parse(report.Text);
                report.Readable = report.Text.Contains("XR Perf Overlay");
            }
            catch (Exception)
            {
                report.Readable = false;
                report.LoadFailed = true;
            }
            // Nom du jeu de secours : le nom du fichier est "date_heure_jeu.txt".
            if (report.Game == "?")
            {
                string name = System.IO.Path.GetFileNameWithoutExtension(path);
                report.Game = name.Length > 20 ? name.Substring(20) : name;
            }
            return report;
        }

        public void Parse(string text)
        {
            string section = "";
            bool inMinutes = false;
            foreach (string rawLine in text.Replace("\r", "").Split('\n'))
            {
                string line = rawLine.TrimStart('﻿');
                if (line.Trim().Length == 0)
                {
                    inMinutes = false;
                    continue;
                }

                if (line.StartsWith("Capteurs indisponibles")) SensorsMissing = true;
                if (line.StartsWith("Mesure réelle du temps GPU écartée")) GpuMeasureRejected = true;
                // Titres de section : en début de ligne, sans " : " (sauf ceux de la répartition et du relevé, qui en contiennent un).
                if (!line.StartsWith(" ") && (!line.Contains(" : ") || line.StartsWith("RÉPARTITION") || line.StartsWith("RELEVÉ")))
                {
                    section = line;
                    if (line.StartsWith("RELEVÉ MINUTE PAR MINUTE"))
                    {
                        inMinutes = true;
                        MinutesGpuMeasured = line.Contains("mesuré");
                    }
                    if (line.StartsWith("RÉPARTITION")) BudgetMs = At(Numbers(line.Substring(line.IndexOf(':') + 1)), 0);
                    continue;
                }

                if (inMinutes)
                {
                    List<double> v = Numbers(line);
                    if (line.TrimStart().StartsWith("minute") || v.Count < 10) continue; // ligne d'en-tête
                    string[] cells = line.Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
                    Minutes.Add(new MinuteRow
                    {
                        Minute = (int)v[0], FpsAvg = v[1], FpsMin = v[2], CpuMs = v[3], GpuMs = v[4],
                        CpuLoad = v[5], GpuLoad = v[6], CpuTemp = v[7], GpuTemp = v[8], VramGb = v[9],
                        GpuEstimated = !MinutesGpuMeasured || (cells.Length > 4 && cells[4].StartsWith("~")),
                    });
                    continue;
                }

                int colon = line.IndexOf(" : ", StringComparison.Ordinal);
                string label = (colon >= 0 ? line.Substring(0, colon) : line).Trim();
                string value = colon >= 0 ? line.Substring(colon + 3).Trim() : "";

                if (section.StartsWith("XR Perf Overlay") || section.Length == 0)
                {
                    switch (label)
                    {
                        case "Jeu": Game = value; break;
                        case "API graphique": Api = value; break;
                        case "Casque": Headset = value.Split(new[] { "   (" }, StringSplitOptions.None)[0]; break;
                        case "Runtime OpenXR": Runtime = value; break;
                        // Jeu SteamVR (OpenVR) mesuré par XRPerfVR : la version de SteamVR.
                        case "Runtime OpenVR": Runtime = value == "--" ? "SteamVR (OpenVR)" : value + " (OpenVR)"; OpenVR = true; break;
                        case "Processeur": Cpu = value == "--" ? "" : value; break;
                        case "Carte graphique": Gpu = value == "--" ? "" : value; break;
                        case "Rendu par œil":
                        {
                            Render = value.Split(new[] { "   (" }, StringSplitOptions.None)[0];
                            Match varied = Regex.Match(value, @"a varié de (\d+ x \d+) à (\d+ x \d+)");
                            if (varied.Success) { RenderMin = varied.Groups[1].Value; RenderMax = varied.Groups[2].Value; }
                            break;
                        }
                        case "Rendu recommandé": RenderRecommended = value.Split(new[] { "   (" }, StringSplitOptions.None)[0]; break;
                        case "Début":
                            if (DateTime.TryParseExact(value, "yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture, DateTimeStyles.None, out DateTime start))
                                Start = start;
                            break;
                        case "Fin": Finished = true; EndText = value; break;
                        case "Dernier relevé": Finished = false; EndText = value.Split(new[] { "  (" }, StringSplitOptions.None)[0]; break;
                        case "Durée":
                            DurationText = value;
                            string[] parts = value.Split(new[] { ", dont " }, StringSplitOptions.None);
                            WallSeconds = Seconds(parts[0]);
                            if (parts.Length > 1) ActiveSeconds = Seconds(parts[1]);
                            break;
                    }
                    continue;
                }

                if (section.StartsWith("IMAGES PAR SECONDE"))
                {
                    List<double> v = Numbers(value);
                    switch (label)
                    {
                        case "Moyenne": FpsAvg = At(v, 0); break;
                        case "Minimum": FpsMin = At(v, 0); break;
                        case "Maximum": FpsMax = At(v, 0); break;
                        case "1 % low": Low1 = At(v, 0); break;
                        case "0,1 % low": Low01 = At(v, 0); break;
                        case "Cible": Target = At(v, 0); TargetHeld = At(v, 1); break;
                        case "Avec framegen": OutFps = At(v, 0); break;
                        case "Images rendues": Frames = At(v, 0); break;
                        case "Coupures": Pauses = At(v, 0); break;
                    }
                    continue;
                }

                if (section.StartsWith("RÉPARTITION"))
                {
                    double share = At(Numbers(value), 0);
                    if (label.StartsWith("Dans le budget")) Distribution[0] = share;
                    else if (label.StartsWith("Jusqu")) Distribution[1] = share;
                    else if (label.StartsWith("De 1,5")) Distribution[2] = share;
                    else if (label.StartsWith("Plus de 2")) Distribution[3] = share;
                    continue;
                }

                // Tableaux "moyenne / max" : intitulé à gauche, puis les nombres.
                string tableLabel = Regex.Replace(line.Trim(), @"\s{2,}.*$", "");
                List<double> row = Numbers(line.Substring(line.IndexOf(tableLabel, StringComparison.Ordinal) + tableLabel.Length));
                if (section.StartsWith("TEMPS PAR IMAGE"))
                {
                    if (tableLabel.StartsWith("Image compl")) { FrameAvg = At(row, 0); FrameMax = At(row, 1); }
                    else if (tableLabel == "CPU") { CpuMsAvg = At(row, 0); CpuMsMax = At(row, 1); }
                    else if (tableLabel.StartsWith("GPU (mesur")) { GpuMeasuredAvg = At(row, 0); GpuMeasuredMax = At(row, 1); }
                    else if (tableLabel.StartsWith("GPU (estim")) { GpuEstimateAvg = At(row, 0); GpuEstimateMax = At(row, 1); }
                }
                else if (section.StartsWith("PROCESSEUR"))
                {
                    if (tableLabel.StartsWith("Charge")) { CpuLoadAvg = At(row, 0); CpuLoadMax = At(row, 1); }
                    else if (tableLabel.StartsWith("Temp")) { CpuTempAvg = At(row, 0); CpuTempMax = At(row, 1); }
                    else if (tableLabel.StartsWith("Fr")) CpuMHzAvg = At(row, 0);
                }
                else if (section.StartsWith("CARTE GRAPHIQUE"))
                {
                    if (tableLabel.StartsWith("Charge")) { GpuLoadAvg = At(row, 0); GpuLoadMax = At(row, 1); }
                    else if (tableLabel.StartsWith("Temp")) { GpuTempAvg = At(row, 0); GpuTempMax = At(row, 1); }
                    else if (tableLabel.StartsWith("Fr")) GpuMHzAvg = At(row, 0);
                    else if (tableLabel.StartsWith("VRAM")) { VramAvg = At(row, 0); VramMax = At(row, 1); VramTotal = At(row, 2); }
                }
            }
        }
    }

    internal enum FindingLevel { Good, Info, Warning, Problem }

    internal sealed class Finding
    {
        public FindingLevel Level;
        public string Text = "";
    }

    /// <summary>
    /// Lecture des chiffres d'un rapport : constats prudents ("semble", "indice"), jamais des certitudes,
    /// car le rapport ne contient que des moyennes et des maximums.
    /// </summary>
    internal static class ReportAnalysis
    {
        /// <summary>
        /// Différences de matériel entre deux rapports (« carte graphique : A → B »), ou chaîne vide si le
        /// matériel est le même ou n'est pas indiqué dans l'un des deux.
        /// </summary>
        public static string HardwareDifference(SessionReport a, SessionReport b)
        {
            var parts = new List<string>();
            if (a.Cpu.Length > 0 && b.Cpu.Length > 0 && !string.Equals(a.Cpu, b.Cpu, StringComparison.OrdinalIgnoreCase))
                parts.Add("processeur " + b.Cpu + " → " + a.Cpu);
            if (a.Gpu.Length > 0 && b.Gpu.Length > 0 && !string.Equals(a.Gpu, b.Gpu, StringComparison.OrdinalIgnoreCase))
                parts.Add("carte graphique " + b.Gpu + " → " + a.Gpu);
            return string.Join(", ", parts);
        }

        /// <summary>Nombre de pixels d'une résolution « 5190 x 4168 » (0 si illisible).</summary>
        public static long Pixels(string render)
        {
            Match m = Regex.Match(render ?? "", @"^(\d+) x (\d+)$");
            return m.Success && long.TryParse(m.Groups[1].Value, out long w) && long.TryParse(m.Groups[2].Value, out long h) ? w * h : 0;
        }

        /// <summary>
        /// Échelle de rendu propre au jeu : largeur rendue / largeur recommandée par le runtime (1.2 = le jeu
        /// rend 20 % plus large et plus haut que prévu). NaN si l'une des deux est inconnue.
        /// </summary>
        public static double RenderScale(SessionReport r)
        {
            Match a = Regex.Match(r.Render ?? "", @"^(\d+) x \d+$"), b = Regex.Match(r.RenderRecommended ?? "", @"^(\d+) x \d+$");
            if (!a.Success || !b.Success) return double.NaN;
            double rendered = double.Parse(a.Groups[1].Value, CultureInfo.InvariantCulture), recommended = double.Parse(b.Groups[1].Value, CultureInfo.InvariantCulture);
            return recommended > 0 && rendered > 0 ? rendered / recommended : double.NaN;
        }

        /// <summary>Le jeu passe-t-il par SteamVR (dont la résolution recommandée suit son propre réglage) ?</summary>
        public static bool ThroughSteamVR(SessionReport r)
        {
            return r.Runtime.IndexOf("SteamVR", StringComparison.OrdinalIgnoreCase) >= 0;
        }

        /// <summary>
        /// Différences de conditions entre deux rapports du même jeu, qui changent les performances sans
        /// rien dire du matériel : résolution de rendu, casque, runtime OpenXR, API graphique. Chaîne vide
        /// si tout est pareil ou non indiqué. `a` est la session récente, `b` la référence.
        /// </summary>
        public static string SetupDifference(SessionReport a, SessionReport b)
        {
            var parts = new List<string>();
            long pa = Pixels(a.Render), pb = Pixels(b.Render);
            if (pa > 0 && pb > 0 && a.Render != b.Render)
            {
                double change = 100.0 * (pa - pb) / pb;
                parts.Add(string.Format(CultureInfo.InvariantCulture, "rendu {0} → {1} par œil ({2}{3} % de pixels)",
                    b.Render, a.Render, change >= 0 ? "+" : "", change.ToString("0", CultureInfo.InvariantCulture)));
            }
            if (a.Headset.Length > 0 && b.Headset.Length > 0 && !string.Equals(a.Headset, b.Headset, StringComparison.OrdinalIgnoreCase))
                parts.Add("casque " + b.Headset + " → " + a.Headset);
            if (a.Runtime.Length > 0 && b.Runtime.Length > 0 && !string.Equals(a.Runtime, b.Runtime, StringComparison.OrdinalIgnoreCase))
                parts.Add("runtime " + b.Runtime + " → " + a.Runtime);
            // L'API graphique n'est pas relevée dans un jeu OpenVR : pas de comparaison possible.
            if (a.Api.Length > 0 && b.Api.Length > 0 && !a.OpenVR && !b.OpenVR && !string.Equals(a.Api, b.Api, StringComparison.OrdinalIgnoreCase))
                parts.Add(b.Api + " → " + a.Api);
            return string.Join(", ", parts);
        }

        private static string F(double value, string format)
        {
            return double.IsNaN(value) ? "--" : value.ToString(format, CultureInfo.InvariantCulture);
        }

        private static void Add(List<Finding> list, FindingLevel level, string text)
        {
            list.Add(new Finding { Level = level, Text = text });
        }

        public static List<Finding> Analyze(SessionReport r, SessionReport previous)
        {
            var findings = new List<Finding>();
            if (!r.Readable)
            {
                Add(findings, FindingLevel.Problem, "Ce fichier n'a pas pu être lu comme un rapport de XR Perf Overlay.");
                return findings;
            }
            if (double.IsNaN(r.FpsAvg) || double.IsNaN(r.Frames) || r.Frames < 100)
            {
                Add(findings, FindingLevel.Info, "Trop peu d'images mesurées pour en tirer quelque chose.");
                return findings;
            }

            double budget = r.Target > 0 ? 1000.0 / r.Target : double.NaN;

            // 1. Cadence.
            if (!double.IsNaN(r.TargetHeld))
            {
                FindingLevel level = r.TargetHeld >= 95 ? FindingLevel.Good : r.TargetHeld >= 80 ? FindingLevel.Warning : FindingLevel.Problem;
                Add(findings, level, string.Format(CultureInfo.InvariantCulture, "La cible de {0} images par seconde a été tenue {1} % du temps (moyenne {2}).",
                    F(r.Target, "0"), F(r.TargetHeld, "0"), F(r.FpsAvg, "0.0")));
            }

            // 2. Ce qui semble limiter.
            double gpuMs = r.GpuMsAvg;
            string gpuKind = r.GpuIsMeasured ? "mesuré" : "estimé";
            // Le temps GPU seul ne suffit pas : mesuré, il compte aussi les attentes de la carte quand le
            // processeur est la limite. La charge de la carte doit confirmer.
            bool gpuBound = r.GpuLoadAvg >= 90 || (!double.IsNaN(budget) && gpuMs >= 0.9 * budget && r.GpuLoadAvg >= 85);
            bool cpuBound = !double.IsNaN(budget) && r.CpuMsAvg >= 0.85 * budget;
            if (gpuBound && !cpuBound)
                Add(findings, FindingLevel.Info, string.Format(CultureInfo.InvariantCulture,
                    "La carte graphique semble être la limite : charge moyenne {0} %, temps GPU {1} {2} ms pour un budget de {3} ms par image.",
                    F(r.GpuLoadAvg, "0"), gpuKind, F(gpuMs, "0.0"), F(budget, "0.0")));
            else if (cpuBound && !gpuBound)
                Add(findings, FindingLevel.Info, string.Format(CultureInfo.InvariantCulture,
                    "Le processeur, ou le jeu lui-même, semble être la limite : temps CPU moyen {0} ms pour un budget de {1} ms, carte graphique chargée à {2} %.",
                    F(r.CpuMsAvg, "0.0"), F(budget, "0.0"), F(r.GpuLoadAvg, "0")));
            else if (cpuBound && gpuBound)
                Add(findings, FindingLevel.Info, string.Format(CultureInfo.InvariantCulture,
                    "Processeur et carte graphique sont tous deux proches de la limite (CPU {0} ms, GPU {1} {2} ms, budget {3} ms).",
                    F(r.CpuMsAvg, "0.0"), gpuKind, F(gpuMs, "0.0"), F(budget, "0.0")));
            else if (r.TargetHeld < 95 && r.GpuLoadMax >= 90)
                Add(findings, FindingLevel.Info, string.Format(CultureInfo.InvariantCulture,
                    "La carte graphique n'est pas saturée en moyenne ({0} %), mais elle l'a été par moments (jusqu'à {1} %) : c'est probablement là que la cadence a baissé.",
                    F(r.GpuLoadAvg, "0"), F(r.GpuLoadMax, "0")));
            else if (r.TargetHeld < 95)
                Add(findings, FindingLevel.Info,
                    "Ni le processeur ni la carte graphique ne sont saturés en moyenne : les baisses viennent plutôt d'à-coups ponctuels (chargements, streaming, scripts du jeu) que d'un manque de puissance.");

            if (r.GpuMeasureImplausible)
                Add(findings, FindingLevel.Warning, string.Format(CultureInfo.InvariantCulture,
                    "Temps GPU mesuré {0} ms pour une carte chargée à {1} % : cette mesure n'est pas représentative avec ce jeu (son rendu est envoyé " +
                    "hors des repères de la layer, cas fréquent des jeux Unreal en Direct3D 12 ou passés par UEVR). L'estimation ({2} ms) est retenue. " +
                    "Depuis la version 10.1, la layer l'écarte d'elle-même ; vous pouvez aussi décocher « Mesurer le vrai temps GPU » pour ce jeu.",
                    F(r.GpuMeasuredAvg, "0.0"), F(r.GpuLoadAvg, "0"), F(r.GpuEstimateAvg, "0.0")));
            else if (r.GpuMeasureRejected)
                Add(findings, FindingLevel.Info,
                    "La mesure réelle du temps GPU a été écartée pendant la session : avec ce jeu, elle n'était pas représentative. " +
                    "Le temps GPU indiqué est l'estimation ; vous pouvez décocher « Mesurer le vrai temps GPU » pour ce jeu.");

            // Écart notable entre mesure et estimation : piste possible, pas une certitude.
            if (r.GpuIsMeasured && r.GpuEstimateAvg > 0 && r.GpuEstimateAvg - r.GpuMeasuredAvg > 0.15 * r.GpuEstimateAvg)
                Add(findings, FindingLevel.Info, string.Format(CultureInfo.InvariantCulture,
                    "Temps GPU estimé {0} ms, mesuré {1} ms : la carte travaille peut-être aussi pour autre chose que le jeu (compositeur du casque, génération d'images, autres programmes).",
                    F(r.GpuEstimateAvg, "0.0"), F(r.GpuMeasuredAvg, "0.0")));

            // 3. Régularité.
            if (!double.IsNaN(r.Low1) && r.Target > 0 && r.Low1 < 0.75 * r.Target)
                Add(findings, FindingLevel.Warning, string.Format(CultureInfo.InvariantCulture,
                    "À-coups : 1 % des images ont duré plus de {0} ms (1 % low à {1} images par seconde).", F(1000.0 / r.Low1, "0.0"), F(r.Low1, "0.0")));
            if (!double.IsNaN(r.Distribution[3]) && r.Distribution[3] >= 1.0)
                Add(findings, FindingLevel.Warning, string.Format(CultureInfo.InvariantCulture,
                    "{0} % des images ont duré plus de deux fois le budget : elles se voient dans le casque (saccades ou reprojection).", F(r.Distribution[3], "0.0")));

            // 4. Températures et mémoire vidéo.
            if (r.CpuTempMax >= 90)
                Add(findings, FindingLevel.Warning, string.Format(CultureInfo.InvariantCulture, "Processeur très chaud : jusqu'à {0} °C.", F(r.CpuTempMax, "0")));
            if (r.GpuTempMax >= 85)
                Add(findings, FindingLevel.Warning, string.Format(CultureInfo.InvariantCulture, "Carte graphique très chaude : jusqu'à {0} °C.", F(r.GpuTempMax, "0")));
            if (r.VramTotal > 0 && r.VramMax / r.VramTotal >= 0.95)
                Add(findings, FindingLevel.Warning, string.Format(CultureInfo.InvariantCulture,
                    "Mémoire vidéo presque pleine : {0} Go sur {1}. Au-delà, le jeu peut saccader.", F(r.VramMax, "0.0"), F(r.VramTotal, "0.0")));

            // 5. Évolution au fil de la session (relevé minute par minute).
            List<MinuteRow> rows = r.Minutes.Where(m => !double.IsNaN(m.FpsAvg)).ToList();
            if (rows.Count >= 9)
            {
                int third = rows.Count / 3;
                double fpsStart = rows.Take(third).Average(m => m.FpsAvg), fpsEnd = rows.Skip(rows.Count - third).Average(m => m.FpsAvg);
                double tempStart = Mean(rows.Take(third).Select(m => m.GpuTemp)), tempEnd = Mean(rows.Skip(rows.Count - third).Select(m => m.GpuTemp));
                double vramStart = Mean(rows.Take(third).Select(m => m.VramGb)), vramEnd = Mean(rows.Skip(rows.Count - third).Select(m => m.VramGb));
                double loadEnd = Mean(rows.Skip(rows.Count - third).Select(m => m.GpuLoad));
                if (fpsEnd < 0.92 * fpsStart)
                {
                    // Une carte qui chauffe n'est pas forcément bridée : on ne parle de limitation thermique
                    // qu'à partir de 83 °C, sinon de saturation si elle tourne à plein.
                    string cause = tempEnd - tempStart >= 8 && tempEnd >= 83
                        ? string.Format(CultureInfo.InvariantCulture, " pendant que la carte graphique chauffe ({0} → {1} °C) : possible limitation thermique",
                            F(tempStart, "0"), F(tempEnd, "0"))
                        : loadEnd >= 90
                            ? string.Format(CultureInfo.InvariantCulture, " avec une carte graphique saturée en fin de session ({0} % de charge)", F(loadEnd, "0"))
                        : vramEnd - vramStart >= 1.5
                            ? string.Format(CultureInfo.InvariantCulture, " pendant que la mémoire vidéo se remplit ({0} → {1} Go)", F(vramStart, "0.0"), F(vramEnd, "0.0"))
                            : "";
                    Add(findings, FindingLevel.Warning, string.Format(CultureInfo.InvariantCulture,
                        "La cadence baisse au fil de la session : {0} images par seconde au début, {1} à la fin{2}.", F(fpsStart, "0.0"), F(fpsEnd, "0.0"), cause));
                }
            }

            // Résolution : le jeu rend-il ce que le runtime lui recommande ?
            double scale = RenderScale(r);
            bool struggling = !double.IsNaN(r.TargetHeld) && r.TargetHeld < 95;
            if (!double.IsNaN(scale) && (scale >= 1.05 || scale <= 0.95))
            {
                double pixels = 100.0 * (scale * scale - 1.0);
                FindingLevel level = scale >= 1.05 && struggling ? FindingLevel.Warning : FindingLevel.Info;
                Add(findings, level, string.Format(CultureInfo.InvariantCulture,
                    "Le jeu rend à {0} % de la résolution recommandée par le runtime ({1}) dans chaque sens, soit {2} % de pixels {3} : c'est sa propre échelle de rendu (option graphique du jeu, vr.PixelDensity pour un jeu Unreal, réglage d'UEVR), pas celle du logiciel du casque.",
                    F(100.0 * scale, "0"), r.RenderRecommended, F(Math.Abs(pixels), "0"), pixels >= 0 ? "en plus" : "en moins"));
            }
            if (ThroughSteamVR(r) && r.RenderRecommended.Length > 0 && struggling)
                Add(findings, FindingLevel.Info, string.Format(CultureInfo.InvariantCulture,
                    "Sous SteamVR, la résolution recommandée ({0}) suit le réglage de résolution de SteamVR (automatique par défaut, qui monte haut sur une carte puissante), pas celui du logiciel du casque.",
                    r.RenderRecommended));

            // Résolution changée par le jeu en cours de session (résolution dynamique, densité adaptative).
            if (r.RenderMin.Length > 0 && r.RenderMax.Length > 0 && r.RenderMin != r.RenderMax)
                Add(findings, FindingLevel.Info, string.Format(CultureInfo.InvariantCulture,
                    "La résolution de rendu a varié pendant la session, de {0} à {1} par œil : le jeu l'ajuste lui-même (résolution dynamique ou densité de pixels adaptative).",
                    r.RenderMin, r.RenderMax));

            // 6. Comparaison avec la session précédente du même jeu.
            if (previous != null && previous.Readable && !double.IsNaN(previous.FpsAvg))
            {
                double delta = r.FpsAvg - previous.FpsAvg;
                double lowDelta = r.Low1 - previous.Low1;
                string when = previous.Start.HasValue ? previous.Start.Value.ToString("dd/MM HH:mm", CultureInfo.InvariantCulture) : "précédente";
                FindingLevel level = delta <= -0.05 * previous.FpsAvg ? FindingLevel.Warning : delta >= 0.05 * previous.FpsAvg ? FindingLevel.Good : FindingLevel.Info;
                string hardware = HardwareDifference(r, previous);
                string setup = SetupDifference(r, previous);
                Add(findings, level, string.Format(CultureInfo.InvariantCulture,
                    "Par rapport à la session du {0} : moyenne {1} ({2}{3}), 1 % low {4} ({5}{6}){7}{8}.",
                    when, F(r.FpsAvg, "0.0"), delta >= 0 ? "+" : "", F(delta, "0.0"), F(r.Low1, "0.0"), lowDelta >= 0 ? "+" : "", F(lowDelta, "0.0"),
                    setup.Length > 0 ? ". Réglages différents : " + setup : "",
                    hardware.Length > 0 ? ". Matériel différent : " + hardware : ""));
            }

            if (r.SensorsMissing)
                Add(findings, FindingLevel.Info, "XRPerfSensors n'était pas lancé : pas de charge, de température ni de temps GPU estimé.");
            return findings;
        }

        internal static double Mean(IEnumerable<double> values)
        {
            List<double> known = values.Where(v => !double.IsNaN(v)).ToList();
            return known.Count == 0 ? double.NaN : known.Average();
        }
    }
    /// <summary>Une partie de la lecture « simple » d'un rapport : un titre et quelques phrases.</summary>
    internal sealed class NoviceSection
    {
        public string Title = "";
        public readonly List<string> Lines = new List<string>();
    }

    /// <summary>Lecture d'un rapport pour quelqu'un qui ne connaît pas le vocabulaire technique.</summary>
    internal sealed class NoviceView
    {
        public FindingLevel Level = FindingLevel.Info;
        public string Verdict = "";
        public string Summary = "";
        public readonly List<NoviceSection> Sections = new List<NoviceSection>();
        public readonly List<string> Tips = new List<string>();
    }

    /// <summary>
    /// Mode « simple » de la fenêtre des rapports : les chiffres du rapport traduits en phrases, avec un
    /// verdict, ce qui a limité, ce qui a pu se voir dans le casque et des pistes concrètes. Mêmes
    /// règles que <see cref="ReportAnalysis"/> ; seule la façon de le dire change.
    /// </summary>
    internal static class NoviceSummary
    {
        private static readonly CultureInfo French = CultureInfo.GetCultureInfo("fr-FR");

        /// <summary>Petit lexique affiché sous la lecture simple.</summary>
        public static readonly string[][] Glossary =
        {
            new[] { "Images par seconde (FPS)", "le nombre d'images que le jeu calcule chaque seconde. Plus il y en a, plus c'est fluide." },
            new[] { "Cible", "le nombre d'images par seconde que le casque attend : sa fréquence d'affichage (72, 90 Hz...)." },
            new[] { "Temps d'image et budget", "le temps mis pour préparer une image, en millisecondes (ms). Pour tenir 72 images par seconde, chaque image doit être prête en 13,9 ms : c'est le budget." },
            new[] { "Processeur (CPU) et carte graphique (GPU)", "le processeur fait tourner le jeu (personnages, physique, préparation des images) ; la carte graphique dessine les images." },
            new[] { "Charge", "la part du temps où le composant travaille. Vers 100 %, il ne peut pas aller plus vite." },
            new[] { "Mémoire vidéo (VRAM)", "la mémoire de la carte graphique, où sont rangées les textures du jeu." },
        };

        private static string N(double value, string format)
        {
            return double.IsNaN(value) ? "--" : value.ToString(format, French);
        }

        // Part d'images dite simplement : "environ 12 images sur 100", ou "3 images sur 1 000" quand elle est petite.
        private static string Share(double percent)
        {
            if (double.IsNaN(percent)) return "--";
            if (percent >= 1.0) return "Environ " + Math.Round(percent).ToString("0", French) + " images sur 100";
            double perThousand = Math.Max(1, Math.Round(percent * 10));
            return perThousand.ToString("0", French) + (perThousand >= 2 ? " images sur 1 000" : " image sur 1 000");
        }

        public static NoviceView Build(SessionReport r, SessionReport previous)
        {
            var view = new NoviceView();
            if (!r.Readable)
            {
                view.Level = FindingLevel.Problem;
                view.Verdict = "Rapport illisible";
                view.Summary = "Ce fichier n'a pas pu être lu comme un rapport de XR Perf Overlay.";
                return view;
            }
            if (double.IsNaN(r.FpsAvg) || double.IsNaN(r.Frames) || r.Frames < 100)
            {
                view.Verdict = "Trop court pour juger";
                view.Summary = "La session a été trop courte pour en tirer quelque chose. Jouez quelques minutes pour obtenir un rapport utile.";
                return view;
            }

            double budget = r.Target > 0 ? 1000.0 / r.Target : double.NaN;
            double held = r.TargetHeld;
            double heavy = r.Distribution[3];

            // 1. Verdict.
            if (double.IsNaN(held))
            {
                view.Level = FindingLevel.Info;
                view.Verdict = "Fluidité non évaluée";
            }
            else if (held >= 95 && !(heavy >= 0.5))
            {
                view.Level = FindingLevel.Good;
                view.Verdict = "Très fluide";
            }
            else if (held >= 85)
            {
                view.Level = FindingLevel.Good;
                view.Verdict = "Fluide, avec quelques à-coups";
            }
            else if (held >= 60)
            {
                view.Level = FindingLevel.Warning;
                view.Verdict = "Correct, mais pas toujours fluide";
            }
            else
            {
                view.Level = FindingLevel.Problem;
                view.Verdict = "Souvent saccadé";
            }

            if (double.IsNaN(r.Target))
                view.Summary = string.Format(French, "Le jeu a calculé en moyenne {0} images par seconde. La fréquence du casque n'est pas connue, donc impossible de dire si c'était suffisant.",
                    N(r.FpsAvg, "0"));
            else if (!double.IsNaN(r.OutFps))
                view.Summary = string.Format(French,
                    "Le jeu devait calculer {0} images par seconde ; votre outil de génération d'images en crée d'autres entre elles, et le casque en a reçu environ {1}. " +
                    "Le jeu a tenu ses {0} images par seconde {2} % du temps (moyenne : {3}).",
                    N(r.Target, "0"), N(r.OutFps, "0"), N(held, "0"), N(r.FpsAvg, "0.0"));
            else
                view.Summary = string.Format(French,
                    "Votre casque attend {0} images par seconde. Le jeu en a calculé {1} en moyenne et a tenu ce rythme {2} % du temps.",
                    N(r.Target, "0"), N(r.FpsAvg, "0.0"), N(held, "0"));

            // 2. Ce qui a pu se voir dans le casque.
            var felt = new NoviceSection { Title = "Ce que vous avez pu ressentir" };
            if (!double.IsNaN(r.Distribution[0]))
            {
                felt.Lines.Add(string.Format(French, "Pour tenir le rythme, chaque image doit être prête en {0} ms. Sur 100 images, environ {1} l'ont été à temps.",
                    N(budget, "0.0"), N(Math.Round(r.Distribution[0]), "0")));
                double late = r.Distribution[1] + r.Distribution[2];
                if (late >= 1.0)
                    felt.Lines.Add(string.Format(French,
                        "{0} étaient un peu en retard. Le casque compense en général en réutilisant l'image précédente : " +
                        "cela peut se voir comme un léger flou ou un dédoublement sur ce qui bouge.", Share(late)));
                if (heavy >= 0.05)
                    felt.Lines.Add(string.Format(French,
                        "{0} ont mis plus du double du temps prévu : ce sont les petits accrocs ou saccades que vous avez pu remarquer.", Share(heavy)));
                else
                    felt.Lines.Add("Pratiquement aucune image n'a pris beaucoup de retard : pas de vraie saccade.");
            }
            else if (!double.IsNaN(r.Low1))
            {
                felt.Lines.Add(string.Format(French, "Les images les plus lentes (1 sur 100) ont mis plus de {0} ms à arriver.", N(1000.0 / r.Low1, "0")));
            }
            if (r.Pauses == 1)
                felt.Lines.Add("Une coupure de plus d'une seconde (chargement, pause) n'a pas été comptée.");
            else if (r.Pauses > 1)
                felt.Lines.Add(string.Format(French, "{0} coupures de plus d'une seconde (chargements, pauses) n'ont pas été comptées.", N(r.Pauses, "0")));
            if (felt.Lines.Count > 0) view.Sections.Add(felt);

            // 3. Ce qui a limité.
            var limit = new NoviceSection { Title = "Qu'est-ce qui a limité ?" };
            double gpuMs = r.GpuMsAvg;
            bool gpuKnown = !double.IsNaN(r.GpuLoadAvg);
            bool gpuBound = r.GpuLoadAvg >= 90 || (!double.IsNaN(budget) && gpuMs >= 0.9 * budget && r.GpuLoadAvg >= 85);
            bool cpuBound = !double.IsNaN(budget) && r.CpuMsAvg >= 0.85 * budget;
            string gpuName = r.Gpu.Length > 0 ? "La carte graphique (" + r.Gpu + ")" : "La carte graphique";
            string cpuName = r.Cpu.Length > 0 ? "Le processeur (" + r.Cpu + ")" : "Le processeur";
            bool struggling = !double.IsNaN(held) && held < 95;
            if (!gpuKnown)
            {
                limit.Lines.Add("XRPerfSensors n'était pas lancé pendant cette partie : impossible de savoir si la carte graphique était au maximum.");
                if (cpuBound)
                    limit.Lines.Add(string.Format(French, "{0}, ou la façon dont le jeu l'utilise, semble avoir fixé la limite : il mettait {1} ms à préparer chaque image, pour {2} ms disponibles.",
                        cpuName, N(r.CpuMsAvg, "0.0"), N(budget, "0.0")));
            }
            else if (gpuBound && cpuBound)
            {
                limit.Lines.Add(string.Format(French, "Le processeur et la carte graphique étaient tous les deux près de leur maximum (carte graphique occupée {0} % du temps).",
                    N(r.GpuLoadAvg, "0")));
            }
            else if (gpuBound)
            {
                limit.Lines.Add(string.Format(French, "{0} travaillait presque sans arrêt ({1} % du temps) : c'est elle qui fixait la limite.", gpuName, N(r.GpuLoadAvg, "0")));
            }
            else if (cpuBound)
            {
                limit.Lines.Add(string.Format(French,
                    "{0}, ou la façon dont le jeu l'utilise, fixait la limite : il mettait {1} ms à préparer chaque image, pour {2} ms disponibles. " +
                    "La carte graphique, elle, n'était occupée que {3} % du temps.",
                    cpuName, N(r.CpuMsAvg, "0.0"), N(budget, "0.0"), N(r.GpuLoadAvg, "0")));
            }
            else if (struggling && r.GpuLoadAvg >= 80)
            {
                limit.Lines.Add(string.Format(French,
                    "La carte graphique était très occupée ({0} % du temps), sans être tout à fait au maximum, et le processeur avait de la marge. " +
                    "Les baisses viennent sans doute des moments les plus chargés, où la carte montait jusqu'à {1} %.", N(r.GpuLoadAvg, "0"), N(r.GpuLoadMax, "0")));
            }
            else if (struggling)
            {
                limit.Lines.Add(string.Format(French,
                    "Ni la carte graphique (occupée {0} % du temps) ni le processeur n'étaient au maximum en moyenne. Les baisses viennent plutôt de moments " +
                    "précis (chargement d'une zone, scène très chargée, sauvegarde) que d'un manque de puissance.", N(r.GpuLoadAvg, "0")));
                if (r.GpuLoadMax >= 90)
                    limit.Lines.Add(string.Format(French, "La carte graphique a quand même atteint {0} % par moments : c'est sans doute là que ça a ralenti.", N(r.GpuLoadMax, "0")));
            }
            else if (r.GpuLoadAvg < 70)
            {
                limit.Lines.Add(string.Format(French, "Rien : votre PC avait de la marge. La carte graphique n'était occupée que {0} % du temps.", N(r.GpuLoadAvg, "0")));
            }
            else
            {
                limit.Lines.Add(string.Format(French, "Rien de bloquant : la carte graphique était bien occupée ({0} % du temps), mais suffisait.", N(r.GpuLoadAvg, "0")));
            }
            if (r.GpuMeasureImplausible || r.GpuMeasureRejected)
                limit.Lines.Add("La mesure exacte du temps de la carte graphique ne fonctionne pas avec ce jeu ; ces explications reposent sur sa charge.");
            view.Sections.Add(limit);

            // 4. Températures, mémoire vidéo, évolution.
            var health = new NoviceSection { Title = "Températures et mémoire" };
            bool cpuHot = r.CpuTempMax >= 90, gpuHot = r.GpuTempMax >= 85;
            if (cpuHot) health.Lines.Add(string.Format(French, "Le processeur a beaucoup chauffé (jusqu'à {0} °C) : il peut alors ralentir pour se protéger.", N(r.CpuTempMax, "0")));
            if (gpuHot) health.Lines.Add(string.Format(French, "La carte graphique a beaucoup chauffé (jusqu'à {0} °C) : elle peut alors ralentir pour se protéger.", N(r.GpuTempMax, "0")));
            if (!cpuHot && !gpuHot && (!double.IsNaN(r.CpuTempMax) || !double.IsNaN(r.GpuTempMax)))
                health.Lines.Add(string.Format(French, "Températures normales (processeur jusqu'à {0} °C, carte graphique jusqu'à {1} °C).",
                    N(r.CpuTempMax, "0"), N(r.GpuTempMax, "0")));
            bool vramFull = r.VramTotal > 0 && r.VramMax / r.VramTotal >= 0.95;
            if (vramFull)
                health.Lines.Add(string.Format(French, "La mémoire de la carte graphique était presque pleine ({0} Go sur {1}). Quand elle déborde, le jeu peut saccader.",
                    N(r.VramMax, "0.0"), N(r.VramTotal, "0.#")));
            else if (r.VramTotal > 0 && !double.IsNaN(r.VramMax))
                health.Lines.Add(string.Format(French, "Mémoire de la carte graphique : {0} Go utilisés au plus, sur {1}. Pas de problème de ce côté.",
                    N(r.VramMax, "0.0"), N(r.VramTotal, "0.#")));

            List<MinuteRow> rows = r.Minutes.Where(m => !double.IsNaN(m.FpsAvg)).ToList();
            bool slowedDown = false, heatedUp = false;
            if (rows.Count >= 9)
            {
                int third = rows.Count / 3;
                double fpsStart = rows.Take(third).Average(m => m.FpsAvg), fpsEnd = rows.Skip(rows.Count - third).Average(m => m.FpsAvg);
                double tempStart = ReportAnalysis.Mean(rows.Take(third).Select(m => m.GpuTemp)), tempEnd = ReportAnalysis.Mean(rows.Skip(rows.Count - third).Select(m => m.GpuTemp));
                if (fpsEnd < 0.92 * fpsStart)
                {
                    slowedDown = true;
                    heatedUp = tempEnd - tempStart >= 8 && tempEnd >= 83;
                    health.Lines.Add(string.Format(French, "Le jeu a ralenti au fil de la partie : {0} images par seconde au début, {1} à la fin{2}.",
                        N(fpsStart, "0"), N(fpsEnd, "0"), heatedUp ? ", pendant que la carte graphique chauffait" : ""));
                }
            }
            if (health.Lines.Count > 0) view.Sections.Add(health);

            // 5. Par rapport à la dernière fois.
            if (previous != null && previous.Readable && !double.IsNaN(previous.FpsAvg))
            {
                var compare = new NoviceSection { Title = "Par rapport à la dernière fois" };
                double delta = r.FpsAvg - previous.FpsAvg;
                string trend = delta >= 0.05 * previous.FpsAvg ? "Mieux" : delta <= -0.05 * previous.FpsAvg ? "Moins bien" : "À peu près pareil";
                string when = previous.Start.HasValue ? previous.Start.Value.ToString("dddd d MMMM 'à' HH:mm", French) : "précédente";
                compare.Lines.Add(string.Format(French, "{0} que la partie du {1} : {2} images par seconde en moyenne, contre {3}.",
                    trend, when, N(r.FpsAvg, "0.0"), N(previous.FpsAvg, "0.0")));
                if (!double.IsNaN(r.TargetHeld) && !double.IsNaN(previous.TargetHeld))
                    compare.Lines.Add(string.Format(French, "Rythme visé tenu {0} % du temps, contre {1} %.", N(r.TargetHeld, "0"), N(previous.TargetHeld, "0")));
                long pixelsNow = ReportAnalysis.Pixels(r.Render), pixelsBefore = ReportAnalysis.Pixels(previous.Render);
                if (pixelsNow > 0 && pixelsBefore > 0 && r.Render != previous.Render)
                {
                    double change = 100.0 * (pixelsNow - pixelsBefore) / pixelsBefore;
                    compare.Lines.Add(string.Format(French, "Attention, l'image n'était pas calculée à la même taille : {0} par œil cette fois, {1} la dernière fois ({2} % de pixels {3}). Plus de pixels, c'est plus de travail pour la carte graphique.",
                        r.Render, previous.Render, N(Math.Abs(change), "0"), change >= 0 ? "en plus" : "en moins"));
                }
                if (r.Runtime.Length > 0 && previous.Runtime.Length > 0 && !string.Equals(r.Runtime, previous.Runtime, StringComparison.OrdinalIgnoreCase))
                    compare.Lines.Add("Attention, le jeu ne passait pas par le même logiciel de VR : " + r.Runtime + " cette fois, " + previous.Runtime + " la dernière fois.");
                string hardware = ReportAnalysis.HardwareDifference(r, previous);
                if (hardware.Length > 0) compare.Lines.Add("Attention, le matériel n'était pas le même : " + hardware + ".");
                view.Sections.Add(compare);
            }

            // 6. Que faire ?
            double scale = ReportAnalysis.RenderScale(r);
            if (!double.IsNaN(scale) && scale >= 1.05 && (gpuBound || struggling))
                view.Tips.Add(string.Format(French, "Le jeu calcule l'image à {0} % de la taille prévue par le logiciel du casque. Remettez son échelle de rendu à 100 % : dans ses options graphiques, ou, pour un jeu Unreal, avec la ligne vr.PixelDensity=1.0 dans son fichier Engine.ini.",
                    N(100.0 * scale, "0")));
            if (ReportAnalysis.ThroughSteamVR(r) && (gpuBound || struggling))
                view.Tips.Add("Le jeu passe par SteamVR : la taille de l'image y dépend du réglage de résolution de SteamVR (automatique par défaut). Fixez-la vous-même dans les réglages vidéo de SteamVR, pour ce jeu ou pour tous.");
            if (!gpuKnown) view.Tips.Add("Lancez XRPerfSensors avant de jouer : le prochain rapport dira si la carte graphique est au maximum.");
            if (gpuBound)
            {
                view.Tips.Add("Baissez la résolution de rendu : dans le logiciel de votre casque, dans les réglages VR du jeu ou dans UEVR. C'est le réglage qui soulage le plus la carte graphique.");
                view.Tips.Add("Baissez les réglages graphiques les plus gourmands : ombres, anti-crénelage, éclairage, distance d'affichage.");
                if (double.IsNaN(r.OutFps))
                    view.Tips.Add("Un outil de génération d'images peut doubler le nombre d'images envoyées au casque.");
            }
            if (cpuBound)
            {
                if (!gpuBound) view.Tips.Add("Baisser la résolution aidera peu : c'est le processeur qui limite, pas la carte graphique.");
                view.Tips.Add("Réduisez ce qui occupe le processeur dans le jeu : foule, circulation, distance d'affichage, ombres des objets en mouvement.");
                view.Tips.Add("Fermez les programmes inutiles pendant la partie (navigateur, enregistrement vidéo, lanceurs de jeux).");
            }
            if ((gpuBound || cpuBound) && struggling)
                view.Tips.Add("Choisissez une fréquence plus basse pour le casque (par exemple 72 Hz au lieu de 90) : chaque image a alors plus de temps pour être prête.");
            if (!gpuBound && !cpuBound && struggling && r.GpuLoadAvg >= 80)
                view.Tips.Add("La carte graphique est souvent près de son maximum : baisser un peu la résolution de rendu lui laisserait de la marge pour les moments chargés.");
            if (!gpuBound && !cpuBound && struggling && gpuKnown && r.GpuLoadAvg < 80)
            {
                view.Tips.Add("Ces à-coups viennent souvent du jeu lui-même (chargements, effets préparés au premier passage) : ils diminuent souvent en rejouant le même passage.");
                view.Tips.Add("Installez le jeu sur un SSD, gardez les pilotes de la carte graphique à jour et fermez les programmes inutiles.");
            }
            if (cpuHot || gpuHot || heatedUp) view.Tips.Add("Vérifiez la ventilation du PC : poussière dans les ventilateurs, boîtier trop fermé, pièce très chaude.");
            if (vramFull) view.Tips.Add("Baissez la qualité des textures ou la résolution de rendu pour soulager la mémoire de la carte graphique.");
            if (slowedDown && !heatedUp && !vramFull) view.Tips.Add("Le ralentissement en cours de partie peut venir du jeu (zone plus chargée) : comparez avec une autre session au même endroit.");
            if (view.Tips.Count == 0)
                view.Tips.Add(!double.IsNaN(r.GpuLoadAvg) && r.GpuLoadAvg < 70 && !struggling
                    ? "Rien à changer. Vous avez même de la marge : vous pouvez essayer d'augmenter un peu la qualité ou la résolution, puis vérifier avec un nouveau rapport."
                    : "Rien à changer : tout s'est bien passé.");
            return view;
        }
    }
}
