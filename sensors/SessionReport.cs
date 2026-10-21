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
        public bool Manual;          // enregistrement démarré et arrêté au raccourci (mode manuel)
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
        /// <summary>Part de la session pendant laquelle les capteurs ont été relevés (en %), NaN si complète ou non indiquée.</summary>
        public double SensorCoverage = double.NaN;
        /// <summary>
        /// Capteurs relevés pendant trop peu de la session (moins de la moitié) : leurs moyennes ne disent rien de la
        /// partie (par exemple un seul relevé, XRPerfSensors lancé à la fin). Elles sont écartées comme si les
        /// capteurs avaient manqué ; le relevé minute par minute les garde, minute par minute.
        /// </summary>
        public bool SensorsTooFew;
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

        // Intitulés des rapports : la layer les écrit en français ou en anglais selon la langue choisie.
        private static bool Is(string label, string french, string english)
        {
            return label == french || label == english;
        }

        private static bool Starts(string text, string french, string english)
        {
            return text.StartsWith(french, StringComparison.Ordinal) || text.StartsWith(english, StringComparison.Ordinal);
        }

        public void Parse(string text)
        {
            string section = "";
            bool inMinutes = false;
            foreach (string rawLine in text.Replace("\r", "").Split('\n'))
            {
                string line = rawLine.TrimStart('\uFEFF');
                if (line.Trim().Length == 0)
                {
                    inMinutes = false;
                    continue;
                }

                if (Starts(line, "Capteurs indisponibles", "Sensors unavailable")) SensorsMissing = true;
                if (Starts(line, "Capteurs relevés pendant", "Sensors read during")) SensorCoverage = At(Numbers(line), 0);
                if (Starts(line, "Mesure réelle du temps GPU écartée", "Real GPU time measurement discarded")) GpuMeasureRejected = true;
                // Titres de section : en début de ligne, sans " : " (sauf ceux de la répartition et du relevé, qui en contiennent un).
                bool distribution = Starts(line, "RÉPARTITION", "FRAME TIME DISTRIBUTION");
                bool minutes = Starts(line, "RELEVÉ MINUTE PAR MINUTE", "MINUTE BY MINUTE");
                if (!line.StartsWith(" ") && (!line.Contains(" : ") || distribution || minutes))
                {
                    section = distribution ? "DIST" : minutes ? "MINUTES"
                            : Starts(line, "IMAGES PAR SECONDE", "FRAMES PER SECOND") ? "FPS"
                            : Starts(line, "TEMPS PAR IMAGE", "FRAME TIME") ? "FRAME"
                            : Starts(line, "PROCESSEUR", "PROCESSOR") ? "CPU"
                            : Starts(line, "CARTE GRAPHIQUE", "GRAPHICS CARD") ? "GPU"
                            : line.StartsWith("XR Perf Overlay", StringComparison.Ordinal) ? "" : "OTHER";
                    if (minutes)
                    {
                        inMinutes = true;
                        MinutesGpuMeasured = line.Contains("mesuré") || line.Contains("measured");
                    }
                    if (distribution) BudgetMs = At(Numbers(line.Substring(line.IndexOf(':') + 1)), 0);
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

                if (section.Length == 0)
                {
                    if (Is(label, "Jeu", "Game")) Game = value;
                    else if (Is(label, "API graphique", "Graphics API")) Api = value;
                    else if (Is(label, "Casque", "Headset")) { Headset = value.Split(new[] { "   (" }, StringSplitOptions.None)[0]; if (Headset == "--") Headset = ""; }
                    else if (Is(label, "Runtime OpenXR", "OpenXR runtime")) Runtime = value == "--" ? "" : value;
                    // Jeu SteamVR (OpenVR) mesuré par XRPerfVR : la version de SteamVR.
                    else if (Is(label, "Runtime OpenVR", "OpenVR runtime")) { Runtime = value == "--" ? "SteamVR (OpenVR)" : value + " (OpenVR)"; OpenVR = true; }
                    else if (Is(label, "Processeur", "Processor")) Cpu = value == "--" ? "" : value;
                    else if (Is(label, "Carte graphique", "Graphics card")) Gpu = value == "--" ? "" : value;
                    else if (Is(label, "Rendu par œil", "Render per eye"))
                    {
                        Render = value.Split(new[] { "   (" }, StringSplitOptions.None)[0];
                        Match varied = Regex.Match(value, @"(?:a varié de|varied from) (\d+ x \d+) (?:à|to) (\d+ x \d+)");
                        if (varied.Success) { RenderMin = varied.Groups[1].Value; RenderMax = varied.Groups[2].Value; }
                    }
                    else if (Is(label, "Rendu recommandé", "Recommended render")) RenderRecommended = value.Split(new[] { "   (" }, StringSplitOptions.None)[0];
                    else if (Is(label, "Début", "Start"))
                    {
                        if (DateTime.TryParseExact(value, "yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture, DateTimeStyles.None, out DateTime start))
                            Start = start;
                    }
                    else if (Is(label, "Fin", "End")) { Finished = true; EndText = value; }
                    else if (Is(label, "Enregistrement", "Recording")) Manual = Starts(value, "manuel", "manual");
                    else if (Is(label, "Dernier relevé", "Last update")) { Finished = false; EndText = value.Split(new[] { "  (" }, StringSplitOptions.None)[0]; }
                    else if (Is(label, "Durée", "Duration"))
                    {
                        DurationText = value;
                        string[] parts = value.Split(new[] { ", dont ", ", of which " }, StringSplitOptions.None);
                        WallSeconds = Seconds(parts[0]);
                        if (parts.Length > 1) ActiveSeconds = Seconds(parts[1]);
                    }
                    continue;
                }

                if (section == "FPS")
                {
                    List<double> v = Numbers(value);
                    if (Is(label, "Moyenne", "Average")) FpsAvg = At(v, 0);
                    else if (label == "Minimum") FpsMin = At(v, 0);
                    else if (label == "Maximum") FpsMax = At(v, 0);
                    else if (label == "1 % low") Low1 = At(v, 0);
                    else if (Is(label, "0,1 % low", "0.1 % low")) Low01 = At(v, 0);
                    else if (Is(label, "Cible", "Target")) { Target = At(v, 0); TargetHeld = At(v, 1); }
                    else if (Is(label, "Avec framegen", "With framegen")) OutFps = At(v, 0);
                    else if (Is(label, "Images rendues", "Frames rendered")) Frames = At(v, 0);
                    else if (Is(label, "Coupures", "Interruptions")) Pauses = At(v, 0);
                    continue;
                }

                if (section == "DIST")
                {
                    double share = At(Numbers(value), 0);
                    if (Starts(label, "Dans le budget", "Within budget")) Distribution[0] = share;
                    else if (Starts(label, "Jusqu", "Up to")) Distribution[1] = share;
                    else if (Starts(label, "De 1,5", "1.5 to 2")) Distribution[2] = share;
                    else if (Starts(label, "Plus de 2", "Over 2")) Distribution[3] = share;
                    continue;
                }

                // Tableaux "moyenne / max" : intitulé à gauche, puis les nombres.
                string tableLabel = Regex.Replace(line.Trim(), @"\s{2,}.*$", "");
                List<double> row = Numbers(line.Substring(line.IndexOf(tableLabel, StringComparison.Ordinal) + tableLabel.Length));
                if (section == "FRAME")
                {
                    if (Starts(tableLabel, "Image compl", "Whole frame")) { FrameAvg = At(row, 0); FrameMax = At(row, 1); }
                    else if (tableLabel == "CPU") { CpuMsAvg = At(row, 0); CpuMsMax = At(row, 1); }
                    else if (Starts(tableLabel, "GPU (mesur", "GPU (measur")) { GpuMeasuredAvg = At(row, 0); GpuMeasuredMax = At(row, 1); }
                    else if (tableLabel.StartsWith("GPU (estim", StringComparison.Ordinal)) { GpuEstimateAvg = At(row, 0); GpuEstimateMax = At(row, 1); }
                }
                else if (section == "CPU")
                {
                    if (Starts(tableLabel, "Charge", "Load")) { CpuLoadAvg = At(row, 0); CpuLoadMax = At(row, 1); }
                    else if (tableLabel.StartsWith("Temp", StringComparison.Ordinal)) { CpuTempAvg = At(row, 0); CpuTempMax = At(row, 1); }
                    else if (Starts(tableLabel, "Fr", "Clock")) CpuMHzAvg = At(row, 0);
                }
                else if (section == "GPU")
                {
                    if (Starts(tableLabel, "Charge", "Load")) { GpuLoadAvg = At(row, 0); GpuLoadMax = At(row, 1); }
                    else if (tableLabel.StartsWith("Temp", StringComparison.Ordinal)) { GpuTempAvg = At(row, 0); GpuTempMax = At(row, 1); }
                    else if (Starts(tableLabel, "Fr", "Clock")) GpuMHzAvg = At(row, 0);
                    else if (tableLabel.StartsWith("VRAM", StringComparison.Ordinal)) { VramAvg = At(row, 0); VramMax = At(row, 1); VramTotal = At(row, 2); }
                }
            }
            if (SensorCoverage < 50) DropSensorAverages();
        }

        // Moyennes et maximums des capteurs écartés (voir SensorsTooFew). La mesure réelle du temps GPU ne vient
        // pas des capteurs : elle est gardée.
        private void DropSensorAverages()
        {
            SensorsTooFew = true;
            CpuLoadAvg = CpuLoadMax = CpuTempAvg = CpuTempMax = CpuMHzAvg = double.NaN;
            GpuLoadAvg = GpuLoadMax = GpuTempAvg = GpuTempMax = GpuMHzAvg = double.NaN;
            VramAvg = VramMax = double.NaN;
            GpuEstimateAvg = GpuEstimateMax = double.NaN;
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
    /// car le rapport ne contient que des moyennes et des maximums. En français ou en anglais.
    /// </summary>
    internal static class ReportAnalysis
    {
        /// <summary>
        /// Différences de matériel entre deux rapports (« carte graphique : A → B »), ou chaîne vide si le
        /// matériel est le même ou n'est pas indiqué dans l'un des deux.
        /// </summary>
        public static string HardwareDifference(SessionReport a, SessionReport b, bool english = false)
        {
            var parts = new List<string>();
            if (a.Cpu.Length > 0 && b.Cpu.Length > 0 && !string.Equals(a.Cpu, b.Cpu, StringComparison.OrdinalIgnoreCase))
                parts.Add((english ? "processor " : "processeur ") + b.Cpu + " → " + a.Cpu);
            if (a.Gpu.Length > 0 && b.Gpu.Length > 0 && !string.Equals(a.Gpu, b.Gpu, StringComparison.OrdinalIgnoreCase))
                parts.Add((english ? "graphics card " : "carte graphique ") + b.Gpu + " → " + a.Gpu);
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
        /// Résolution de rendu conseillée d'après le temps GPU et le budget d'une image (voir <see cref="AdviseResolution"/>).
        /// </summary>
        internal sealed class ResolutionAdvice
        {
            /// <summary>Échelle par côté, par rapport à la résolution de la session : 0,85 = 85 % en largeur et en hauteur.</summary>
            public double Scale;
            /// <summary>Résolution par œil correspondante (0 si celle de la session est inconnue).</summary>
            public int Width, Height;
            /// <summary>Marge : la résolution peut monter (sinon, elle doit baisser).</summary>
            public bool Up;
            /// <summary>Il faudrait descendre sous <see cref="MinScale"/> : la résolution seule ne suffira pas.</summary>
            public bool NotEnough;
            /// <summary>Part des pixels de la session (Scale²), en %.</summary>
            public double PixelPercent { get { return 100.0 * Scale * Scale; } }
        }

        private const double MinScale = 0.6, MaxScale = 1.3;

        /// <summary>
        /// Conseil chiffré sur la résolution de rendu, ou null s'il n'y a rien de sûr à dire. Le temps GPU d'une
        /// image suit à peu près le nombre de pixels : pour qu'il tienne dans le budget avec 10 % de marge, il faut
        /// multiplier chaque côté par √(0,9 × budget / temps GPU). Seulement quand la carte graphique limite (vers
        /// le bas), ou quand elle a clairement de la marge et que la cible est tenue (vers le haut). Une partie du
        /// travail ne dépend pas des pixels : le calcul est un peu optimiste, d'où la marge et l'arrondi vers le
        /// bas au pas de 5 %.
        /// </summary>
        public static ResolutionAdvice AdviseResolution(SessionReport r)
        {
            double budget = r.Target > 0 ? 1000.0 / r.Target : double.NaN;
            double gpuMs = r.GpuMsAvg;
            if (double.IsNaN(budget) || double.IsNaN(gpuMs) || gpuMs <= 0 || double.IsNaN(r.GpuLoadAvg)) return null;
            bool gpuBound = r.GpuLoadAvg >= 90 || (gpuMs >= 0.9 * budget && r.GpuLoadAvg >= 85);
            bool cpuBound = r.CpuMsAvg >= 0.85 * budget;
            var advice = new ResolutionAdvice();
            if (gpuBound)
            {
                // Processeur aussi à la limite : baisser la résolution ne suffirait pas à tenir la cible, pas de chiffre.
                if (cpuBound) return null;
                // Carte chargée mais temps GPU dans le budget et cible tenue : rien à chiffrer.
                if (!(gpuMs >= 0.9 * budget || r.TargetHeld < 95)) return null;
                double scale = Math.Min(Math.Sqrt(0.9 * budget / gpuMs), 0.95);
                if (scale < MinScale)
                {
                    scale = MinScale;
                    advice.NotEnough = true;
                }
                advice.Scale = Math.Floor(scale * 20.0 + 1e-9) / 20.0;
            }
            else if (!cpuBound && r.TargetHeld >= 95 && r.GpuLoadAvg < 70)
            {
                double scale = Math.Min(Math.Sqrt(0.85 * budget / gpuMs), MaxScale);
                scale = Math.Floor(scale * 20.0 + 1e-9) / 20.0;
                if (scale < 1.05) return null;
                advice.Scale = scale;
                advice.Up = true;
            }
            else
            {
                return null;
            }
            Match m = Regex.Match(AdviceBase(r), @"^(\d+) x (\d+)$");
            if (m.Success)
            {
                advice.Width = (int)Math.Round(int.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture) * advice.Scale);
                advice.Height = (int)Math.Round(int.Parse(m.Groups[2].Value, CultureInfo.InvariantCulture) * advice.Scale);
            }
            return advice;
        }

        /// <summary>
        /// Résolution par œil sur laquelle porte le conseil : celle que le jeu a rendue, ou, pour un jeu SteamVR
        /// (dont l'image n'est pas visible de l'extérieur), celle que SteamVR recommande. Vide si inconnue.
        /// </summary>
        public static string AdviceBase(SessionReport r)
        {
            if (Pixels(r.Render) > 0) return r.Render;
            if (r.OpenVR && Pixels(r.RenderRecommended) > 0) return r.RenderRecommended;
            return "";
        }

        /// <summary>
        /// Cadence que le processeur peut suivre (images par seconde), d'après son temps moyen par image et 10 %
        /// de marge. NaN si le temps CPU est inconnu.
        /// </summary>
        public static double CpuSustainableFps(SessionReport r)
        {
            return r.CpuMsAvg > 0 ? 1000.0 / (r.CpuMsAvg * 1.1) : double.NaN;
        }

        /// <summary>
        /// Différences de conditions entre deux rapports du même jeu, qui changent les performances sans
        /// rien dire du matériel : résolution de rendu, casque, runtime OpenXR, API graphique. Chaîne vide
        /// si tout est pareil ou non indiqué. `a` est la session récente, `b` la référence.
        /// </summary>
        public static string SetupDifference(SessionReport a, SessionReport b, bool english = false)
        {
            var parts = new List<string>();
            // Jeux SteamVR : la résolution recommandée par SteamVR, faute de celle du jeu.
            string ra = AdviceBase(a), rb = AdviceBase(b);
            long pa = Pixels(ra), pb = Pixels(rb);
            if (pa > 0 && pb > 0 && ra != rb)
            {
                double change = 100.0 * (pa - pb) / pb;
                parts.Add(string.Format(CultureInfo.InvariantCulture, english ? "render {0} → {1} per eye ({2}{3} % pixels)" : "rendu {0} → {1} par œil ({2}{3} % de pixels)",
                    rb, ra, change >= 0 ? "+" : "", change.ToString("0", CultureInfo.InvariantCulture)));
            }
            if (a.Headset.Length > 0 && b.Headset.Length > 0 && !string.Equals(a.Headset, b.Headset, StringComparison.OrdinalIgnoreCase))
                parts.Add((english ? "headset " : "casque ") + b.Headset + " → " + a.Headset);
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

        public static List<Finding> Analyze(SessionReport r, SessionReport previous, bool english = false)
        {
            Func<string, string, string> L = (fr, en) => english ? en : fr;
            var findings = new List<Finding>();
            if (!r.Readable)
            {
                Add(findings, FindingLevel.Problem, L("Ce fichier n'a pas pu être lu comme un rapport de XR Perf Overlay.",
                    "This file could not be read as an XR Perf Overlay report."));
                return findings;
            }
            if (double.IsNaN(r.FpsAvg) || double.IsNaN(r.Frames) || r.Frames < 100)
            {
                Add(findings, FindingLevel.Info, L("Trop peu d'images mesurées pour en tirer quelque chose.", "Too few frames were measured to draw anything from them."));
                return findings;
            }

            double budget = r.Target > 0 ? 1000.0 / r.Target : double.NaN;

            // 1. Cadence.
            if (!double.IsNaN(r.TargetHeld))
            {
                FindingLevel level = r.TargetHeld >= 95 ? FindingLevel.Good : r.TargetHeld >= 80 ? FindingLevel.Warning : FindingLevel.Problem;
                Add(findings, level, string.Format(CultureInfo.InvariantCulture,
                    L("La cible de {0} images par seconde a été tenue {1} % du temps (moyenne {2}).", "The {0} frames per second target was held {1} % of the time (average {2})."),
                    F(r.Target, "0"), F(r.TargetHeld, "0"), F(r.FpsAvg, "0.0")));
            }

            // 2. Ce qui semble limiter.
            double gpuMs = r.GpuMsAvg;
            string gpuKind = r.GpuIsMeasured ? L("mesuré", "measured") : L("estimé", "estimated");
            // Le temps GPU seul ne suffit pas : mesuré, il compte aussi les attentes de la carte quand le
            // processeur est la limite. La charge de la carte doit confirmer.
            bool gpuBound = r.GpuLoadAvg >= 90 || (!double.IsNaN(budget) && gpuMs >= 0.9 * budget && r.GpuLoadAvg >= 85);
            bool cpuBound = !double.IsNaN(budget) && r.CpuMsAvg >= 0.85 * budget;
            if (gpuBound && !cpuBound)
                Add(findings, FindingLevel.Info, string.Format(CultureInfo.InvariantCulture,
                    L("La carte graphique semble être la limite : charge moyenne {0} %, temps GPU {1} {2} ms pour un budget de {3} ms par image.",
                      "The graphics card seems to be the bottleneck: average load {0} %, {1} GPU time {2} ms for a budget of {3} ms per frame."),
                    F(r.GpuLoadAvg, "0"), gpuKind, F(gpuMs, "0.0"), F(budget, "0.0")));
            else if (cpuBound && !gpuBound)
                Add(findings, FindingLevel.Info, string.Format(CultureInfo.InvariantCulture,
                    L("Le processeur, ou le jeu lui-même, semble être la limite : temps CPU moyen {0} ms pour un budget de {1} ms, carte graphique chargée à {2} %.",
                      "The processor, or the game itself, seems to be the bottleneck: average CPU time {0} ms for a budget of {1} ms, graphics card at {2} % load."),
                    F(r.CpuMsAvg, "0.0"), F(budget, "0.0"), F(r.GpuLoadAvg, "0")));
            else if (cpuBound && gpuBound)
                Add(findings, FindingLevel.Info, string.Format(CultureInfo.InvariantCulture,
                    L("Processeur et carte graphique sont tous deux proches de la limite (CPU {0} ms, GPU {1} {2} ms, budget {3} ms).",
                      "Processor and graphics card are both close to the limit (CPU {0} ms, {1} GPU {2} ms, budget {3} ms)."),
                    F(r.CpuMsAvg, "0.0"), gpuKind, F(gpuMs, "0.0"), F(budget, "0.0")));
            else if (r.TargetHeld < 95 && r.GpuLoadMax >= 90)
                Add(findings, FindingLevel.Info, string.Format(CultureInfo.InvariantCulture,
                    L("La carte graphique n'est pas saturée en moyenne ({0} %), mais elle l'a été par moments (jusqu'à {1} %) : c'est probablement là que la cadence a baissé.",
                      "The graphics card is not saturated on average ({0} %), but it was at times (up to {1} %): that is probably when the frame rate dropped."),
                    F(r.GpuLoadAvg, "0"), F(r.GpuLoadMax, "0")));
            else if (r.TargetHeld < 95)
                Add(findings, FindingLevel.Info,
                    L("Ni le processeur ni la carte graphique ne sont saturés en moyenne : les baisses viennent plutôt d'à-coups ponctuels (chargements, streaming, scripts du jeu) que d'un manque de puissance.",
                      "Neither the processor nor the graphics card is saturated on average: the drops come from occasional hitches (loading, streaming, game scripts) rather than a lack of power."));

            if (r.GpuMeasureImplausible)
                Add(findings, FindingLevel.Warning, string.Format(CultureInfo.InvariantCulture,
                    L("Temps GPU mesuré {0} ms pour une carte chargée à {1} % : cette mesure n'est pas représentative avec ce jeu (son rendu est envoyé " +
                      "hors des repères de la layer, cas fréquent des jeux Unreal en Direct3D 12 ou passés par UEVR). L'estimation ({2} ms) est retenue. " +
                      "La layer l'écarte désormais d'elle-même ; vous pouvez aussi décocher « Mesurer le vrai temps GPU » pour ce jeu.",
                      "Measured GPU time {0} ms for a card at {1} % load: this measurement is not representative with this game (its rendering is submitted " +
                      "outside the layer's markers, common with Unreal games in Direct3D 12 or run through UEVR). The estimate ({2} ms) is used instead. " +
                      "The layer now discards it on its own; you can also turn off « Measure the real GPU time » for this game."),
                    F(r.GpuMeasuredAvg, "0.0"), F(r.GpuLoadAvg, "0"), F(r.GpuEstimateAvg, "0.0")));
            else if (r.GpuMeasureRejected)
                Add(findings, FindingLevel.Info,
                    L("La mesure réelle du temps GPU a été écartée pendant la session : avec ce jeu, elle n'était pas représentative. " +
                      "Le temps GPU indiqué est l'estimation ; vous pouvez décocher « Mesurer le vrai temps GPU » pour ce jeu.",
                      "The real GPU time measurement was discarded during the session: with this game, it was not representative. " +
                      "The GPU time shown is the estimate; you can turn off « Measure the real GPU time » for this game."));

            // Écart notable entre mesure et estimation : piste possible, pas une certitude.
            if (r.GpuIsMeasured && r.GpuEstimateAvg > 0 && r.GpuEstimateAvg - r.GpuMeasuredAvg > 0.15 * r.GpuEstimateAvg)
                Add(findings, FindingLevel.Info, string.Format(CultureInfo.InvariantCulture,
                    L("Temps GPU estimé {0} ms, mesuré {1} ms : la carte travaille peut-être aussi pour autre chose que le jeu (compositeur du casque, génération d'images, autres programmes).",
                      "GPU time estimated {0} ms, measured {1} ms: the card may also be working on something other than the game (headset compositor, frame generation, other programs)."),
                    F(r.GpuEstimateAvg, "0.0"), F(r.GpuMeasuredAvg, "0.0")));

            // 3. Régularité.
            if (!double.IsNaN(r.Low1) && r.Target > 0 && r.Low1 < 0.75 * r.Target)
                Add(findings, FindingLevel.Warning, string.Format(CultureInfo.InvariantCulture,
                    L("À-coups : 1 % des images ont duré plus de {0} ms (1 % low à {1} images par seconde).", "Hitches: 1 % of frames took longer than {0} ms (1 % low at {1} frames per second)."),
                    F(1000.0 / r.Low1, "0.0"), F(r.Low1, "0.0")));
            if (!double.IsNaN(r.Distribution[3]) && r.Distribution[3] >= 1.0)
                Add(findings, FindingLevel.Warning, string.Format(CultureInfo.InvariantCulture,
                    L("{0} % des images ont duré plus de deux fois le budget : elles se voient dans le casque (saccades ou reprojection).",
                      "{0} % of frames took more than twice the budget: they show in the headset (stutter or reprojection)."),
                    F(r.Distribution[3], "0.0")));

            // 4. Températures et mémoire vidéo.
            if (r.CpuTempMax >= 90)
                Add(findings, FindingLevel.Warning, string.Format(CultureInfo.InvariantCulture, L("Processeur très chaud : jusqu'à {0} °C.", "Very hot processor: up to {0} °C."), F(r.CpuTempMax, "0")));
            if (r.GpuTempMax >= 85)
                Add(findings, FindingLevel.Warning, string.Format(CultureInfo.InvariantCulture, L("Carte graphique très chaude : jusqu'à {0} °C.", "Very hot graphics card: up to {0} °C."), F(r.GpuTempMax, "0")));
            if (r.VramTotal > 0 && r.VramMax / r.VramTotal >= 0.95)
                Add(findings, FindingLevel.Warning, string.Format(CultureInfo.InvariantCulture,
                    L("Mémoire vidéo presque pleine : {0} Go sur {1}. Au-delà, le jeu peut saccader.", "Video memory almost full: {0} GB of {1}. Beyond that, the game can stutter."),
                    F(r.VramMax, "0.0"), F(r.VramTotal, "0.0")));

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
                        ? string.Format(CultureInfo.InvariantCulture, L(" pendant que la carte graphique chauffe ({0} → {1} °C) : possible limitation thermique",
                            " while the graphics card heats up ({0} → {1} °C): possible thermal throttling"), F(tempStart, "0"), F(tempEnd, "0"))
                        : loadEnd >= 90
                            ? string.Format(CultureInfo.InvariantCulture, L(" avec une carte graphique saturée en fin de session ({0} % de charge)",
                                " with a saturated graphics card at the end of the session ({0} % load)"), F(loadEnd, "0"))
                        : vramEnd - vramStart >= 1.5
                            ? string.Format(CultureInfo.InvariantCulture, L(" pendant que la mémoire vidéo se remplit ({0} → {1} Go)", " while video memory fills up ({0} → {1} GB)"),
                                F(vramStart, "0.0"), F(vramEnd, "0.0"))
                            : "";
                    Add(findings, FindingLevel.Warning, string.Format(CultureInfo.InvariantCulture,
                        L("La cadence baisse au fil de la session : {0} images par seconde au début, {1} à la fin{2}.", "The frame rate drops over the session: {0} frames per second at the start, {1} at the end{2}."),
                        F(fpsStart, "0.0"), F(fpsEnd, "0.0"), cause));
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
                    L("Le jeu rend à {0} % de la résolution recommandée par le runtime ({1}) dans chaque sens, soit {2} % de pixels {3} : c'est sa propre échelle de rendu (option graphique du jeu, vr.PixelDensity pour un jeu Unreal, réglage d'UEVR), pas celle du logiciel du casque.",
                      "The game renders at {0} % of the resolution recommended by the runtime ({1}) in each direction, that is {2} % {3} pixels: this is its own render scale (game graphics option, vr.PixelDensity for an Unreal game, UEVR setting), not the headset software's."),
                    F(100.0 * scale, "0"), r.RenderRecommended, F(Math.Abs(pixels), "0"), pixels >= 0 ? L("en plus", "more") : L("en moins", "fewer")));
            }
            if (ThroughSteamVR(r) && r.RenderRecommended.Length > 0 && struggling)
                Add(findings, FindingLevel.Info, string.Format(CultureInfo.InvariantCulture,
                    L("Sous SteamVR, la résolution recommandée ({0}) suit le réglage de résolution de SteamVR (automatique par défaut, qui monte haut sur une carte puissante), pas celui du logiciel du casque.",
                      "Under SteamVR, the recommended resolution ({0}) follows SteamVR's resolution setting (automatic by default, which goes high on a powerful card), not the headset software's."),
                    r.RenderRecommended));

            // Conseil chiffré : résolution qui ferait tenir la cible, ou marge disponible.
            ResolutionAdvice advice = AdviseResolution(r);
            if (advice != null)
            {
                string size = advice.Width > 0 ? string.Format(CultureInfo.InvariantCulture, L(" ({0} x {1} par œil)", " ({0} x {1} per eye)"), advice.Width, advice.Height) : "";
                if (advice.Up)
                    Add(findings, FindingLevel.Good, string.Format(CultureInfo.InvariantCulture,
                        L("Marge : la carte graphique a travaillé {0} ms par image pour un budget de {1} ms. La résolution de rendu pourrait monter à environ {2} % par côté{3}, soit {4} % de pixels, en tenant encore la cible.",
                          "Headroom: the graphics card worked {0} ms per frame for a budget of {1} ms. The render resolution could go up to about {2} % per side{3}, that is {4} % of the pixels, while still holding the target."),
                        F(gpuMs, "0.0"), F(budget, "0.0"), F(100.0 * advice.Scale, "0"), size, F(advice.PixelPercent, "0")));
                else if (advice.NotEnough)
                    Add(findings, FindingLevel.Warning, string.Format(CultureInfo.InvariantCulture,
                        L("Temps GPU {0} ms pour un budget de {1} ms : même à {2} % de la résolution par côté{3}, la cible ne serait probablement pas tenue. Il faut aussi baisser la qualité graphique, une fréquence plus basse ou une génération d'images.",
                          "GPU time {0} ms for a budget of {1} ms: even at {2} % of the resolution per side{3}, the target would probably not be held. Graphics quality, a lower refresh rate or frame generation are needed as well."),
                        F(gpuMs, "0.0"), F(budget, "0.0"), F(100.0 * advice.Scale, "0"), size));
                else
                    Add(findings, FindingLevel.Info, string.Format(CultureInfo.InvariantCulture,
                        L("Pour tenir la cible avec un peu de marge, la résolution de rendu devrait descendre à environ {0} % par côté{1}, soit {2} % de pixels (temps GPU {3} ms pour un budget de {4} ms). C'est une estimation : à vérifier avec un nouveau rapport.",
                          "To hold the target with a little headroom, the render resolution should come down to about {0} % per side{1}, that is {2} % of the pixels (GPU time {3} ms for a budget of {4} ms). This is an estimate: check it with a new report."),
                        F(100.0 * advice.Scale, "0"), size, F(advice.PixelPercent, "0"), F(gpuMs, "0.0"), F(budget, "0.0")));
            }
            if (cpuBound && CpuSustainableFps(r) < r.Target)
                Add(findings, FindingLevel.Info, string.Format(CultureInfo.InvariantCulture,
                    L("Avec {0} ms par image en moyenne, le processeur peut suivre environ {1} images par seconde, pour {2} visées.",
                      "At {0} ms per frame on average, the processor can keep up with about {1} frames per second, for {2} targeted."),
                    F(r.CpuMsAvg, "0.0"), F(Math.Floor(CpuSustainableFps(r)), "0"), F(r.Target, "0")));

            // Résolution changée par le jeu en cours de session (résolution dynamique, densité adaptative).
            if (r.RenderMin.Length > 0 && r.RenderMax.Length > 0 && r.RenderMin != r.RenderMax)
                Add(findings, FindingLevel.Info, string.Format(CultureInfo.InvariantCulture,
                    L("La résolution de rendu a varié pendant la session, de {0} à {1} par œil : le jeu l'ajuste lui-même (résolution dynamique ou densité de pixels adaptative).",
                      "The render resolution varied during the session, from {0} to {1} per eye: the game adjusts it itself (dynamic resolution or adaptive pixel density)."),
                    r.RenderMin, r.RenderMax));

            // 6. Comparaison avec la session précédente du même jeu.
            if (previous != null && previous.Readable && !double.IsNaN(previous.FpsAvg))
            {
                double delta = r.FpsAvg - previous.FpsAvg;
                double lowDelta = r.Low1 - previous.Low1;
                string when = previous.Start.HasValue ? previous.Start.Value.ToString("dd/MM HH:mm", CultureInfo.InvariantCulture) : L("précédente", "before");
                FindingLevel level = delta <= -0.05 * previous.FpsAvg ? FindingLevel.Warning : delta >= 0.05 * previous.FpsAvg ? FindingLevel.Good : FindingLevel.Info;
                string hardware = HardwareDifference(r, previous, english);
                string setup = SetupDifference(r, previous, english);
                Add(findings, level, string.Format(CultureInfo.InvariantCulture,
                    L("Par rapport à la session du {0} : moyenne {1} ({2}{3}), 1 % low {4} ({5}{6}){7}{8}.", "Compared with the session of {0}: average {1} ({2}{3}), 1 % low {4} ({5}{6}){7}{8}."),
                    when, F(r.FpsAvg, "0.0"), delta >= 0 ? "+" : "", F(delta, "0.0"), F(r.Low1, "0.0"), lowDelta >= 0 ? "+" : "", F(lowDelta, "0.0"),
                    setup.Length > 0 ? L(". Réglages différents : ", ". Different settings: ") + setup : "",
                    hardware.Length > 0 ? L(". Matériel différent : ", ". Different hardware: ") + hardware : ""));
            }

            if (r.SensorsMissing)
                Add(findings, FindingLevel.Info, L("XRPerfSensors n'était pas lancé : pas de charge, de température ni de temps GPU estimé.",
                    "XRPerfSensors was not running: no load, temperature or estimated GPU time."));
            else if (r.SensorsTooFew)
                Add(findings, FindingLevel.Warning, string.Format(CultureInfo.InvariantCulture,
                    L("XRPerfSensors n'a relevé les capteurs que pendant {0} % de la session (lancé en cours de partie, ou à la fin) : leurs moyennes ne représentent pas la partie et ne sont pas utilisées. Charge, températures et temps GPU estimé sont donc inconnus ; lancez XRPerfSensors avant le jeu.",
                      "XRPerfSensors read the sensors during only {0} % of the session (started mid-game, or at the end): their averages do not represent the session and are not used. Load, temperatures and estimated GPU time are therefore unknown; start XRPerfSensors before the game."),
                    F(r.SensorCoverage, "0")));
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
        /// <summary>Nature de la partie, pour son illustration : felt, limit, health, compare.</summary>
        public string Kind = "";
        public string Title = "";
        public readonly List<string> Lines = new List<string>();
    }

    /// <summary>Lecture d'un rapport pour quelqu'un qui ne connaît pas le vocabulaire technique.</summary>
    internal sealed class NoviceView
    {
        public FindingLevel Level = FindingLevel.Info;
        public string Verdict = "";
        public string Summary = "";
        /// <summary>Ce qui a limité : gpu, cpu, both, busy (carte très occupée), spikes (à-coups), none, unknown.</summary>
        public string Limiter = "";
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
        private static readonly CultureInfo English = CultureInfo.GetCultureInfo("en-US");

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

        public static readonly string[][] GlossaryEnglish =
        {
            new[] { "Frames per second (FPS)", "the number of images the game computes each second. The more there are, the smoother it is." },
            new[] { "Target", "the number of frames per second the headset expects: its refresh rate (72, 90 Hz...)." },
            new[] { "Frame time and budget", "the time taken to prepare one frame, in milliseconds (ms). To hold 72 frames per second, each frame must be ready in 13.9 ms: that is the budget." },
            new[] { "Processor (CPU) and graphics card (GPU)", "the processor runs the game (characters, physics, preparing frames); the graphics card draws the images." },
            new[] { "Load", "the share of time the component is working. Near 100 %, it cannot go any faster." },
            new[] { "Video memory (VRAM)", "the graphics card's memory, where the game's textures are stored." },
        };

        public static NoviceView Build(SessionReport r, SessionReport previous, bool english = false)
        {
            CultureInfo culture = english ? English : French;
            Func<string, string, string> L = (fr, en) => english ? en : fr;
            Func<double, string, string> N = (value, format) => double.IsNaN(value) ? "--" : value.ToString(format, culture);
            // Part d'images dite simplement : "environ 12 images sur 100", ou "3 images sur 1 000" quand elle est petite.
            Func<double, string> Share = percent =>
            {
                if (double.IsNaN(percent)) return "--";
                if (percent >= 1.0) return L("Environ ", "About ") + Math.Round(percent).ToString("0", culture) + L(" images sur 100", " frames out of 100");
                double perThousand = Math.Max(1, Math.Round(percent * 10));
                return perThousand.ToString("0", culture) + (perThousand >= 2 ? L(" images sur 1 000", " frames out of 1,000") : L(" image sur 1 000", " frame out of 1,000"));
            };

            var view = new NoviceView();
            if (!r.Readable)
            {
                view.Level = FindingLevel.Problem;
                view.Verdict = L("Rapport illisible", "Unreadable report");
                view.Summary = L("Ce fichier n'a pas pu être lu comme un rapport de XR Perf Overlay.", "This file could not be read as an XR Perf Overlay report.");
                return view;
            }
            if (double.IsNaN(r.FpsAvg) || double.IsNaN(r.Frames) || r.Frames < 100)
            {
                view.Verdict = L("Trop court pour juger", "Too short to judge");
                view.Summary = L("La session a été trop courte pour en tirer quelque chose. Jouez quelques minutes pour obtenir un rapport utile.",
                    "The session was too short to draw anything from it. Play for a few minutes to get a useful report.");
                return view;
            }

            double budget = r.Target > 0 ? 1000.0 / r.Target : double.NaN;
            double held = r.TargetHeld;
            double heavy = r.Distribution[3];

            // 1. Verdict.
            if (double.IsNaN(held))
            {
                view.Level = FindingLevel.Info;
                view.Verdict = L("Fluidité non évaluée", "Smoothness not assessed");
            }
            else if (held >= 95 && !(heavy >= 0.5))
            {
                view.Level = FindingLevel.Good;
                view.Verdict = L("Très fluide", "Very smooth");
            }
            else if (held >= 85)
            {
                view.Level = FindingLevel.Good;
                view.Verdict = L("Fluide, avec quelques à-coups", "Smooth, with a few hitches");
            }
            else if (held >= 60)
            {
                view.Level = FindingLevel.Warning;
                view.Verdict = L("Correct, mais pas toujours fluide", "Decent, but not always smooth");
            }
            else
            {
                view.Level = FindingLevel.Problem;
                view.Verdict = L("Souvent saccadé", "Often stuttering");
            }

            if (double.IsNaN(r.Target))
                view.Summary = string.Format(culture, L("Le jeu a calculé en moyenne {0} images par seconde. La fréquence du casque n'est pas connue, donc impossible de dire si c'était suffisant.",
                    "The game computed {0} frames per second on average. The headset's refresh rate is unknown, so there is no telling whether that was enough."),
                    N(r.FpsAvg, "0"));
            else if (!double.IsNaN(r.OutFps))
                view.Summary = string.Format(culture,
                    L("Le jeu devait calculer {0} images par seconde ; votre outil de génération d'images en crée d'autres entre elles, et le casque en a reçu environ {1}. " +
                      "Le jeu a tenu ses {0} images par seconde {2} % du temps (moyenne : {3}).",
                      "The game had to compute {0} frames per second; your frame generation tool creates more in between, and the headset received about {1}. " +
                      "The game held its {0} frames per second {2} % of the time (average: {3})."),
                    N(r.Target, "0"), N(r.OutFps, "0"), N(held, "0"), N(r.FpsAvg, "0.0"));
            else
                view.Summary = string.Format(culture,
                    L("Votre casque attend {0} images par seconde. Le jeu en a calculé {1} en moyenne et a tenu ce rythme {2} % du temps.",
                      "Your headset expects {0} frames per second. The game computed {1} on average and held that pace {2} % of the time."),
                    N(r.Target, "0"), N(r.FpsAvg, "0.0"), N(held, "0"));

            // 2. Ce qui a pu se voir dans le casque.
            var felt = new NoviceSection { Kind = "felt", Title = L("Ce que vous avez pu ressentir", "What you may have felt") };
            if (!double.IsNaN(r.Distribution[0]))
            {
                felt.Lines.Add(string.Format(culture, L("Pour tenir le rythme, chaque image doit être prête en {0} ms. Sur 100 images, environ {1} l'ont été à temps.",
                    "To keep the pace, each frame must be ready in {0} ms. Out of 100 frames, about {1} were on time."),
                    N(budget, "0.0"), N(Math.Round(r.Distribution[0]), "0")));
                double late = r.Distribution[1] + r.Distribution[2];
                if (late >= 1.0)
                    felt.Lines.Add(string.Format(culture,
                        L("{0} étaient un peu en retard. Le casque compense en général en réutilisant l'image précédente : " +
                          "cela peut se voir comme un léger flou ou un dédoublement sur ce qui bouge.",
                          "{0} were a little late. The headset usually makes up for it by reusing the previous frame: " +
                          "this can show as slight blur or doubling on moving things."), Share(late)));
                if (heavy >= 0.05)
                    felt.Lines.Add(string.Format(culture,
                        L("{0} ont mis plus du double du temps prévu : ce sont les petits accrocs ou saccades que vous avez pu remarquer.",
                          "{0} took more than twice the expected time: those are the small hitches or stutters you may have noticed."), Share(heavy)));
                else
                    felt.Lines.Add(L("Pratiquement aucune image n'a pris beaucoup de retard : pas de vraie saccade.", "Almost no frame was very late: no real stutter."));
            }
            else if (!double.IsNaN(r.Low1))
            {
                felt.Lines.Add(string.Format(culture, L("Les images les plus lentes (1 sur 100) ont mis plus de {0} ms à arriver.", "The slowest frames (1 in 100) took more than {0} ms to arrive."),
                    N(1000.0 / r.Low1, "0")));
            }
            if (r.Pauses == 1)
                felt.Lines.Add(L("Une coupure de plus d'une seconde (chargement, pause) n'a pas été comptée.", "One interruption longer than a second (loading, pause) was not counted."));
            else if (r.Pauses > 1)
                felt.Lines.Add(string.Format(culture, L("{0} coupures de plus d'une seconde (chargements, pauses) n'ont pas été comptées.",
                    "{0} interruptions longer than a second (loading, pauses) were not counted."), N(r.Pauses, "0")));
            if (felt.Lines.Count > 0) view.Sections.Add(felt);

            // 3. Ce qui a limité.
            var limit = new NoviceSection { Kind = "limit", Title = L("Qu'est-ce qui a limité ?", "What held it back?") };
            double gpuMs = r.GpuMsAvg;
            bool gpuKnown = !double.IsNaN(r.GpuLoadAvg);
            bool gpuBound = r.GpuLoadAvg >= 90 || (!double.IsNaN(budget) && gpuMs >= 0.9 * budget && r.GpuLoadAvg >= 85);
            bool cpuBound = !double.IsNaN(budget) && r.CpuMsAvg >= 0.85 * budget;
            string gpuName = r.Gpu.Length > 0 ? L("La carte graphique (", "The graphics card (") + r.Gpu + ")" : L("La carte graphique", "The graphics card");
            string cpuName = r.Cpu.Length > 0 ? L("Le processeur (", "The processor (") + r.Cpu + ")" : L("Le processeur", "The processor");
            bool struggling = !double.IsNaN(held) && held < 95;
            if (!gpuKnown)
            {
                view.Limiter = cpuBound ? "cpu" : "unknown";
                limit.Lines.Add(r.SensorsTooFew
                    ? string.Format(culture, L("XRPerfSensors n'a relevé les capteurs que pendant {0} % de cette partie : trop peu pour savoir si la carte graphique était au maximum.",
                        "XRPerfSensors read the sensors during only {0} % of this session: too little to tell whether the graphics card was at its maximum."), N(r.SensorCoverage, "0"))
                    : L("XRPerfSensors n'était pas lancé pendant cette partie : impossible de savoir si la carte graphique était au maximum.",
                        "XRPerfSensors was not running during this session: there is no telling whether the graphics card was at its maximum."));
                if (cpuBound)
                    limit.Lines.Add(string.Format(culture, L("{0}, ou la façon dont le jeu l'utilise, semble avoir fixé la limite : il mettait {1} ms à préparer chaque image, pour {2} ms disponibles.",
                        "{0}, or the way the game uses it, seems to have set the limit: it took {1} ms to prepare each frame, with {2} ms available."),
                        cpuName, N(r.CpuMsAvg, "0.0"), N(budget, "0.0")));
            }
            else if (gpuBound && cpuBound)
            {
                view.Limiter = "both";
                limit.Lines.Add(string.Format(culture, L("Le processeur et la carte graphique étaient tous les deux près de leur maximum (carte graphique occupée {0} % du temps).",
                    "The processor and the graphics card were both near their maximum (graphics card busy {0} % of the time)."),
                    N(r.GpuLoadAvg, "0")));
            }
            else if (gpuBound)
            {
                view.Limiter = "gpu";
                limit.Lines.Add(string.Format(culture, L("{0} travaillait presque sans arrêt ({1} % du temps) : c'est elle qui fixait la limite.",
                    "{0} was working almost non-stop ({1} % of the time): it was setting the limit."), gpuName, N(r.GpuLoadAvg, "0")));
            }
            else if (cpuBound)
            {
                view.Limiter = "cpu";
                limit.Lines.Add(string.Format(culture,
                    L("{0}, ou la façon dont le jeu l'utilise, fixait la limite : il mettait {1} ms à préparer chaque image, pour {2} ms disponibles. " +
                      "La carte graphique, elle, n'était occupée que {3} % du temps.",
                      "{0}, or the way the game uses it, was setting the limit: it took {1} ms to prepare each frame, with {2} ms available. " +
                      "The graphics card was only busy {3} % of the time."),
                    cpuName, N(r.CpuMsAvg, "0.0"), N(budget, "0.0"), N(r.GpuLoadAvg, "0")));
            }
            else if (struggling && r.GpuLoadAvg >= 80)
            {
                view.Limiter = "busy";
                limit.Lines.Add(string.Format(culture,
                    L("La carte graphique était très occupée ({0} % du temps), sans être tout à fait au maximum, et le processeur avait de la marge. " +
                      "Les baisses viennent sans doute des moments les plus chargés, où la carte montait jusqu'à {1} %.",
                      "The graphics card was very busy ({0} % of the time), without quite reaching its maximum, and the processor had headroom. " +
                      "The drops probably come from the busiest moments, when the card went up to {1} %."), N(r.GpuLoadAvg, "0"), N(r.GpuLoadMax, "0")));
            }
            else if (struggling)
            {
                view.Limiter = "spikes";
                limit.Lines.Add(string.Format(culture,
                    L("Ni la carte graphique (occupée {0} % du temps) ni le processeur n'étaient au maximum en moyenne. Les baisses viennent plutôt de moments " +
                      "précis (chargement d'une zone, scène très chargée, sauvegarde) que d'un manque de puissance.",
                      "Neither the graphics card (busy {0} % of the time) nor the processor was at its maximum on average. The drops come from specific " +
                      "moments (loading an area, a very busy scene, saving) rather than a lack of power."), N(r.GpuLoadAvg, "0")));
                if (r.GpuLoadMax >= 90)
                    limit.Lines.Add(string.Format(culture, L("La carte graphique a quand même atteint {0} % par moments : c'est sans doute là que ça a ralenti.",
                        "The graphics card still reached {0} % at times: that is probably when it slowed down."), N(r.GpuLoadMax, "0")));
            }
            else if (r.GpuLoadAvg < 70)
            {
                view.Limiter = "none";
                limit.Lines.Add(string.Format(culture, L("Rien : votre PC avait de la marge. La carte graphique n'était occupée que {0} % du temps.",
                    "Nothing: your PC had headroom. The graphics card was only busy {0} % of the time."), N(r.GpuLoadAvg, "0")));
            }
            else
            {
                view.Limiter = "none";
                limit.Lines.Add(string.Format(culture, L("Rien de bloquant : la carte graphique était bien occupée ({0} % du temps), mais suffisait.",
                    "Nothing blocking: the graphics card was well used ({0} % of the time), but it was enough."), N(r.GpuLoadAvg, "0")));
            }
            if (r.GpuMeasureImplausible || r.GpuMeasureRejected)
                limit.Lines.Add(L("La mesure exacte du temps de la carte graphique ne fonctionne pas avec ce jeu ; ces explications reposent sur sa charge.",
                    "Exact measurement of the graphics card's time does not work with this game; these explanations rely on its load."));
            view.Sections.Add(limit);

            // 4. Températures, mémoire vidéo, évolution.
            var health = new NoviceSection { Kind = "health", Title = L("Températures et mémoire", "Temperatures and memory") };
            bool cpuHot = r.CpuTempMax >= 90, gpuHot = r.GpuTempMax >= 85;
            if (cpuHot) health.Lines.Add(string.Format(culture, L("Le processeur a beaucoup chauffé (jusqu'à {0} °C) : il peut alors ralentir pour se protéger.",
                "The processor got very hot (up to {0} °C): it can then slow down to protect itself."), N(r.CpuTempMax, "0")));
            if (gpuHot) health.Lines.Add(string.Format(culture, L("La carte graphique a beaucoup chauffé (jusqu'à {0} °C) : elle peut alors ralentir pour se protéger.",
                "The graphics card got very hot (up to {0} °C): it can then slow down to protect itself."), N(r.GpuTempMax, "0")));
            if (!cpuHot && !gpuHot && (!double.IsNaN(r.CpuTempMax) || !double.IsNaN(r.GpuTempMax)))
                health.Lines.Add(string.Format(culture, L("Températures normales (processeur jusqu'à {0} °C, carte graphique jusqu'à {1} °C).",
                    "Normal temperatures (processor up to {0} °C, graphics card up to {1} °C)."),
                    N(r.CpuTempMax, "0"), N(r.GpuTempMax, "0")));
            bool vramFull = r.VramTotal > 0 && r.VramMax / r.VramTotal >= 0.95;
            if (vramFull)
                health.Lines.Add(string.Format(culture, L("La mémoire de la carte graphique était presque pleine ({0} Go sur {1}). Quand elle déborde, le jeu peut saccader.",
                    "The graphics card's memory was almost full ({0} GB of {1}). When it overflows, the game can stutter."),
                    N(r.VramMax, "0.0"), N(r.VramTotal, "0.#")));
            else if (r.VramTotal > 0 && !double.IsNaN(r.VramMax))
                health.Lines.Add(string.Format(culture, L("Mémoire de la carte graphique : {0} Go utilisés au plus, sur {1}. Pas de problème de ce côté.",
                    "Graphics card memory: {0} GB used at most, of {1}. No problem there."),
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
                    health.Lines.Add(string.Format(culture, L("Le jeu a ralenti au fil de la partie : {0} images par seconde au début, {1} à la fin{2}.",
                        "The game slowed down over the session: {0} frames per second at the start, {1} at the end{2}."),
                        N(fpsStart, "0"), N(fpsEnd, "0"), heatedUp ? L(", pendant que la carte graphique chauffait", ", while the graphics card was heating up") : ""));
                }
            }
            if (health.Lines.Count > 0) view.Sections.Add(health);

            // 5. Par rapport à la dernière fois.
            if (previous != null && previous.Readable && !double.IsNaN(previous.FpsAvg))
            {
                var compare = new NoviceSection { Kind = "compare", Title = L("Par rapport à la dernière fois", "Compared with last time") };
                double delta = r.FpsAvg - previous.FpsAvg;
                string trend = delta >= 0.05 * previous.FpsAvg ? L("Mieux", "Better") : delta <= -0.05 * previous.FpsAvg ? L("Moins bien", "Worse") : L("À peu près pareil", "About the same");
                string when = previous.Start.HasValue
                    ? previous.Start.Value.ToString(english ? "dddd, MMMM d 'at' HH:mm" : "dddd d MMMM 'à' HH:mm", culture)
                    : L("précédente", "before");
                compare.Lines.Add(string.Format(culture, L("{0} que la partie du {1} : {2} images par seconde en moyenne, contre {3}.",
                    "{0} than the session of {1}: {2} frames per second on average, against {3}."),
                    trend, when, N(r.FpsAvg, "0.0"), N(previous.FpsAvg, "0.0")));
                if (!double.IsNaN(r.TargetHeld) && !double.IsNaN(previous.TargetHeld))
                    compare.Lines.Add(string.Format(culture, L("Rythme visé tenu {0} % du temps, contre {1} %.", "Target pace held {0} % of the time, against {1} %."),
                        N(r.TargetHeld, "0"), N(previous.TargetHeld, "0")));
                string renderNow = ReportAnalysis.AdviceBase(r), renderBefore = ReportAnalysis.AdviceBase(previous);
                long pixelsNow = ReportAnalysis.Pixels(renderNow), pixelsBefore = ReportAnalysis.Pixels(renderBefore);
                if (pixelsNow > 0 && pixelsBefore > 0 && renderNow != renderBefore)
                {
                    double change = 100.0 * (pixelsNow - pixelsBefore) / pixelsBefore;
                    compare.Lines.Add(string.Format(culture, L("Attention, l'image n'était pas calculée à la même taille : {0} par œil cette fois, {1} la dernière fois ({2} % de pixels {3}). Plus de pixels, c'est plus de travail pour la carte graphique.",
                        "Note that the image was not computed at the same size: {0} per eye this time, {1} last time ({2} % {3} pixels). More pixels means more work for the graphics card."),
                        renderNow, renderBefore, N(Math.Abs(change), "0"), change >= 0 ? L("en plus", "more") : L("en moins", "fewer")));
                }
                if (r.Runtime.Length > 0 && previous.Runtime.Length > 0 && !string.Equals(r.Runtime, previous.Runtime, StringComparison.OrdinalIgnoreCase))
                    compare.Lines.Add(L("Attention, le jeu ne passait pas par le même logiciel de VR : ", "Note that the game did not go through the same VR software: ") +
                        r.Runtime + L(" cette fois, ", " this time, ") + previous.Runtime + L(" la dernière fois.", " last time."));
                string hardware = ReportAnalysis.HardwareDifference(r, previous, english);
                if (hardware.Length > 0) compare.Lines.Add(L("Attention, le matériel n'était pas le même : ", "Note that the hardware was not the same: ") + hardware + ".");
                view.Sections.Add(compare);
            }

            // 6. Que faire ?
            double scale = ReportAnalysis.RenderScale(r);
            if (!double.IsNaN(scale) && scale >= 1.05 && (gpuBound || struggling))
                view.Tips.Add(string.Format(culture, L("Le jeu calcule l'image à {0} % de la taille prévue par le logiciel du casque. Remettez son échelle de rendu à 100 % : dans ses options graphiques, ou, pour un jeu Unreal, avec la ligne vr.PixelDensity=1.0 dans son fichier Engine.ini.",
                    "The game computes the image at {0} % of the size planned by the headset software. Set its render scale back to 100 %: in its graphics options, or, for an Unreal game, with the line vr.PixelDensity=1.0 in its Engine.ini file."),
                    N(100.0 * scale, "0")));
            if (ReportAnalysis.ThroughSteamVR(r) && (gpuBound || struggling))
                view.Tips.Add(L("Le jeu passe par SteamVR : la taille de l'image y dépend du réglage de résolution de SteamVR (automatique par défaut). Fixez-la vous-même dans les réglages vidéo de SteamVR, pour ce jeu ou pour tous.",
                    "The game goes through SteamVR: the image size there depends on SteamVR's resolution setting (automatic by default). Set it yourself in SteamVR's video settings, for this game or for all."));
            if (!gpuKnown) view.Tips.Add(L("Lancez XRPerfSensors avant de jouer : le prochain rapport dira si la carte graphique est au maximum.",
                "Start XRPerfSensors before playing: the next report will tell whether the graphics card is at its maximum."));
            ReportAnalysis.ResolutionAdvice advice = ReportAnalysis.AdviseResolution(r);
            string whereToSet = ReportAnalysis.ThroughSteamVR(r)
                ? L("dans les réglages vidéo de SteamVR (le jeu passe par lui), dans les réglages VR du jeu ou dans UEVR",
                    "in SteamVR's video settings (the game goes through it), in the game's VR settings or in UEVR")
                : L("dans le logiciel de votre casque, dans les réglages VR du jeu ou dans UEVR", "in your headset software, in the game's VR settings or in UEVR");
            string adviceSize = advice != null && advice.Width > 0
                ? string.Format(culture, L(", soit environ {0} x {1} par œil au lieu de {2}", ", that is about {0} x {1} per eye instead of {2}"), advice.Width, advice.Height, ReportAnalysis.AdviceBase(r))
                : "";
            if (gpuBound)
            {
                if (advice != null && !advice.Up && !advice.NotEnough)
                    view.Tips.Add(string.Format(culture,
                        L("Baissez la résolution de rendu à environ {0} % de l'actuelle en largeur et en hauteur{1} ({2} % des pixels) : d'après le travail de la carte graphique, c'est ce qu'il faut pour tenir le rythme avec un peu de marge. Réglez-la {3}, puis vérifiez avec un nouveau rapport : c'est une estimation.",
                          "Lower the render resolution to about {0} % of the current one in width and height{1} ({2} % of the pixels): judging by the graphics card's work, that is what it takes to hold the pace with a little headroom. Set it {3}, then check with a new report: this is an estimate."),
                        N(100.0 * advice.Scale, "0"), adviceSize, N(advice.PixelPercent, "0"), whereToSet));
                else if (advice != null && advice.NotEnough)
                    view.Tips.Add(string.Format(culture,
                        L("La résolution seule ne suffira pas : même réduite à {0} % en largeur et en hauteur{1}, la carte graphique serait encore trop lente. Baissez-la ({2}) et, en plus, la qualité graphique, ou passez à une fréquence plus basse ou à une génération d'images.",
                          "Resolution alone will not be enough: even reduced to {0} % in width and height{1}, the graphics card would still be too slow. Lower it ({2}) and also the graphics quality, or switch to a lower refresh rate or frame generation."),
                        N(100.0 * advice.Scale, "0"), adviceSize, whereToSet));
                else
                    view.Tips.Add(L("Baissez la résolution de rendu : dans le logiciel de votre casque, dans les réglages VR du jeu ou dans UEVR. C'est le réglage qui soulage le plus la carte graphique.",
                        "Lower the render resolution: in your headset software, in the game's VR settings or in UEVR. It is the setting that relieves the graphics card the most."));
                view.Tips.Add(L("Baissez les réglages graphiques les plus gourmands : ombres, anti-crénelage, éclairage, distance d'affichage.",
                    "Lower the most demanding graphics settings: shadows, anti-aliasing, lighting, draw distance."));
                if (double.IsNaN(r.OutFps))
                    view.Tips.Add(L("Un outil de génération d'images peut doubler le nombre d'images envoyées au casque.", "A frame generation tool can double the number of frames sent to the headset."));
            }
            if (cpuBound)
            {
                if (!gpuBound) view.Tips.Add(L("Baisser la résolution aidera peu : c'est le processeur qui limite, pas la carte graphique.",
                    "Lowering the resolution will help little: the processor is the bottleneck, not the graphics card."));
                double cpuFps = ReportAnalysis.CpuSustainableFps(r);
                if (cpuFps < r.Target)
                {
                    string more = double.IsNaN(r.OutFps) && cpuFps >= 0.55 * r.Target
                        ? string.Format(culture, L(" Avec une génération d'images, le jeu n'aurait plus à en calculer que {0} par seconde, ce qui est à sa portée.",
                            " With frame generation, the game would only have to compute {0} per second, which is within its reach."), N(r.Target / 2.0, "0"))
                        : "";
                    view.Tips.Add(string.Format(culture,
                        L("Le processeur prépare une image en {0} ms en moyenne : il peut suivre environ {1} images par seconde, pour {2} visées.{3}",
                          "The processor prepares a frame in {0} ms on average: it can keep up with about {1} frames per second, for {2} targeted.{3}"),
                        N(r.CpuMsAvg, "0.0"), N(Math.Floor(cpuFps), "0"), N(r.Target, "0"), more));
                }
                view.Tips.Add(L("Réduisez ce qui occupe le processeur dans le jeu : foule, circulation, distance d'affichage, ombres des objets en mouvement.",
                    "Reduce what keeps the processor busy in the game: crowds, traffic, draw distance, shadows of moving objects."));
                view.Tips.Add(L("Fermez les programmes inutiles pendant la partie (navigateur, enregistrement vidéo, lanceurs de jeux).",
                    "Close unneeded programs while playing (browser, video recording, game launchers)."));
            }
            if ((gpuBound || cpuBound) && struggling)
                view.Tips.Add(L("Choisissez une fréquence plus basse pour le casque (par exemple 72 Hz au lieu de 90) : chaque image a alors plus de temps pour être prête.",
                    "Choose a lower refresh rate for the headset (for example 72 Hz instead of 90): each frame then has more time to be ready."));
            if (!gpuBound && !cpuBound && struggling && r.GpuLoadAvg >= 80)
                view.Tips.Add(L("La carte graphique est souvent près de son maximum : baisser un peu la résolution de rendu lui laisserait de la marge pour les moments chargés.",
                    "The graphics card is often near its maximum: lowering the render resolution a little would leave it headroom for busy moments."));
            if (!gpuBound && !cpuBound && struggling && gpuKnown && r.GpuLoadAvg < 80)
            {
                view.Tips.Add(L("Ces à-coups viennent souvent du jeu lui-même (chargements, effets préparés au premier passage) : ils diminuent souvent en rejouant le même passage.",
                    "These hitches often come from the game itself (loading, effects prepared the first time through): they often decrease when replaying the same part."));
                view.Tips.Add(L("Installez le jeu sur un SSD, gardez les pilotes de la carte graphique à jour et fermez les programmes inutiles.",
                    "Install the game on an SSD, keep the graphics card drivers up to date and close unneeded programs."));
            }
            if (cpuHot || gpuHot || heatedUp) view.Tips.Add(L("Vérifiez la ventilation du PC : poussière dans les ventilateurs, boîtier trop fermé, pièce très chaude.",
                "Check the PC's cooling: dust in the fans, a closed-in case, a very hot room."));
            if (vramFull) view.Tips.Add(L("Baissez la qualité des textures ou la résolution de rendu pour soulager la mémoire de la carte graphique.",
                "Lower texture quality or the render resolution to relieve the graphics card's memory."));
            if (slowedDown && !heatedUp && !vramFull) view.Tips.Add(L("Le ralentissement en cours de partie peut venir du jeu (zone plus chargée) : comparez avec une autre session au même endroit.",
                "The slowdown during the session may come from the game (a busier area): compare with another session in the same place."));
            if (view.Tips.Count == 0 && advice != null && advice.Up)
                view.Tips.Add(string.Format(culture,
                    L("Rien à changer, et vous avez de la marge : vous pourriez monter la résolution de rendu jusqu'à environ {0} % de l'actuelle en largeur et en hauteur{1}, ou la qualité graphique, puis vérifier avec un nouveau rapport.",
                      "Nothing to change, and you have headroom: you could raise the render resolution up to about {0} % of the current one in width and height{1}, or the graphics quality, then check with a new report."),
                    N(100.0 * advice.Scale, "0"), adviceSize));
            if (view.Tips.Count == 0)
                view.Tips.Add(!double.IsNaN(r.GpuLoadAvg) && r.GpuLoadAvg < 70 && !struggling
                    ? L("Rien à changer. Vous avez même de la marge : vous pouvez essayer d'augmenter un peu la qualité ou la résolution, puis vérifier avec un nouveau rapport.",
                        "Nothing to change. You even have headroom: you can try raising the quality or resolution a little, then check with a new report.")
                    : L("Rien à changer : tout s'est bien passé.", "Nothing to change: everything went well."));
            return view;
        }
    }
}
