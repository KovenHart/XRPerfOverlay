// Fenêtre "Rapports de session" : liste des rapports .txt écrits par la layer, présentation et
// analyse de chacun, courbes du relevé minute par minute, comparaison de deux sessions, suppression.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Windows.Forms;

namespace XRPerfSensors
{
    internal sealed class ReportsWindow : Form
    {
        private const string AllGames = "Tous les jeux";

        private readonly Func<IList<string>> _folders; // dossiers à parcourir, recalculés à chaque actualisation
        private readonly Settings _settings;           // choix de la lecture simple ou détaillée, mémorisé
        private readonly RadioButton _simple = new RadioButton { Text = "Simple", AutoSize = true, Margin = new Padding(3, 5, 3, 3) };
        private readonly RadioButton _detailed = new RadioButton { Text = "Détaillé", AutoSize = true, Margin = new Padding(3, 5, 3, 3) };
        private readonly ComboBox _gameFilter = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 230 };
        private readonly ListView _list = new ListView
        {
            View = View.Details,
            FullRowSelect = true,
            MultiSelect = true,
            HideSelection = false,
            Dock = DockStyle.Fill,
        };
        private readonly TabControl _tabs = new TabControl { Dock = DockStyle.Fill };
        private readonly RichTextBox _analysis = new RichTextBox { Dock = DockStyle.Fill, ReadOnly = true, BorderStyle = BorderStyle.None, BackColor = SystemColors.Window };
        private readonly ReportChart _chart = new ReportChart { Dock = DockStyle.Fill };
        private readonly ListView _compare = new ListView { View = View.Details, FullRowSelect = true, Dock = DockStyle.Fill };
        private readonly Label _compareHint = new Label { Dock = DockStyle.Top, AutoSize = false, Height = 38, TextAlign = ContentAlignment.MiddleLeft, ForeColor = SystemColors.GrayText };
        private readonly TextBox _raw = new TextBox
        {
            Dock = DockStyle.Fill,
            Multiline = true,
            ReadOnly = true,
            ScrollBars = ScrollBars.Both,
            WordWrap = false,
            BackColor = SystemColors.Window,
            Font = new Font(FontFamily.GenericMonospace, 9f),
        };
        private readonly Label _status = new Label { Dock = DockStyle.Bottom, AutoSize = false, Height = 22, TextAlign = ContentAlignment.MiddleLeft, Padding = new Padding(6, 0, 0, 0) };
        private readonly Button _delete = new Button { Text = "Supprimer", AutoSize = true };
        private readonly Button _notepad = new Button { Text = "Ouvrir dans le Bloc-notes", AutoSize = true };
        private readonly Button _folder = new Button { Text = "Ouvrir le dossier", AutoSize = true };

        private readonly Dictionary<string, SessionReport> _cache = new Dictionary<string, SessionReport>(StringComparer.OrdinalIgnoreCase);
        private List<SessionReport> _reports = new List<SessionReport>();
        private bool _filling;
        private string _signature; // dossiers et fichiers vus à la dernière actualisation (nom, date, taille)
        private readonly Font _regular, _bold, _title, _mono, _verdict; // polices de l'onglet Analyse

        public ReportsWindow(Func<IList<string>> folders, Settings settings)
        {
            _folders = folders;
            _settings = settings;
            Text = "XR Perf Overlay " + AppVersion.Text + " - rapports de session";
            _regular = new Font(Font.FontFamily, 9.5f);
            _bold = new Font(Font.FontFamily, 9.5f, FontStyle.Bold);
            _title = new Font(Font.FontFamily, 13f, FontStyle.Bold);
            _mono = new Font(FontFamily.GenericMonospace, 9.5f);
            _verdict = new Font(Font.FontFamily, 12f, FontStyle.Bold);
            Icon = AppIcon.Load(new Size(32, 32));
            StartPosition = FormStartPosition.CenterScreen;
            Size = new Size(1180, 760);
            MinimumSize = new Size(820, 520);

            // Colonne de gauche : filtre, liste, boutons.
            _list.Columns.Add("Date", 118);
            _list.Columns.Add("Jeu", 150);
            _list.Columns.Add("Durée", 64, HorizontalAlignment.Right);
            _list.Columns.Add("FPS moy.", 66, HorizontalAlignment.Right);
            _list.Columns.Add("1 % low", 62, HorizontalAlignment.Right);
            _list.Columns.Add("Cible tenue", 76, HorizontalAlignment.Right);
            _list.SelectedIndexChanged += (s, e) =>
            {
                if (!_filling) ShowSelection();
            };
            _list.KeyDown += (s, e) =>
            {
                if (e.KeyCode == Keys.Delete) DeleteSelected();
            };

            var filter = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, WrapContents = true, Padding = new Padding(0, 2, 0, 4) };
            filter.Controls.Add(new Label { Text = "Jeu :", AutoSize = true, Margin = new Padding(3, 7, 3, 3) });
            filter.Controls.Add(_gameFilter);
            // Lecture simple (phrases, sans vocabulaire technique) ou détaillée (tous les chiffres).
            filter.Controls.Add(new Label { Text = "Lecture :", AutoSize = true, Margin = new Padding(16, 7, 3, 3) });
            filter.Controls.Add(_simple);
            filter.Controls.Add(_detailed);
            _simple.Checked = settings == null || settings.ReportsSimple;
            _detailed.Checked = !_simple.Checked;
            _simple.CheckedChanged += (s, e) =>
            {
                if (_settings != null && _settings.ReportsSimple != _simple.Checked)
                {
                    _settings.ReportsSimple = _simple.Checked;
                    _settings.Save();
                }
                ShowSelection();
            };
            _gameFilter.SelectedIndexChanged += (s, e) =>
            {
                if (!_filling) FillList();
            };

            var refresh = new Button { Text = "Actualiser", AutoSize = true };
            refresh.Click += (s, e) => RefreshReports(true);
            _notepad.Click += (s, e) => OpenSelectedInNotepad();
            _folder.Click += (s, e) => OpenFolder();
            _delete.Click += (s, e) => DeleteSelected();
            var buttons = new FlowLayoutPanel { Dock = DockStyle.Bottom, AutoSize = true, WrapContents = true, Padding = new Padding(0, 4, 0, 0) };
            buttons.Controls.AddRange(new Control[] { refresh, _notepad, _folder, _delete });

            var left = new Panel { Dock = DockStyle.Fill };
            left.Controls.Add(_list);   // ajouté en premier : il occupe la place laissée par les deux autres
            left.Controls.Add(buttons);
            left.Controls.Add(filter);

            // Partie droite : onglets.
            AddTab("Analyse", _analysis);
            AddTab("Courbes", _chart);
            var comparePanel = new Panel { Dock = DockStyle.Fill };
            comparePanel.Controls.Add(_compare);     // ajouté en premier : il occupe la place laissée par l'indication
            comparePanel.Controls.Add(_compareHint);
            AddTab("Comparaison", comparePanel);
            AddTab("Texte", _raw);
            _compare.Columns.Add("", 210);
            _compare.Columns.Add("Session A", 140, HorizontalAlignment.Right);
            _compare.Columns.Add("Session B", 140, HorizontalAlignment.Right);
            _compare.Columns.Add("Écart (A - B)", 120, HorizontalAlignment.Right);

            var split = new SplitContainer { Dock = DockStyle.Fill, Orientation = Orientation.Vertical, SplitterWidth = 6 };
            split.Panel1.Controls.Add(left);
            split.Panel2.Controls.Add(_tabs);
            Controls.Add(split);
            Controls.Add(_status);
            Load += (s, e) => split.SplitterDistance = Math.Min(560, Math.Max(300, ClientSize.Width * 45 / 100));

            // Retour au premier plan : un jeu a pu écrire ou terminer un rapport entre-temps.
            Activated += (s, e) => RefreshReports(false);
        }

        private void AddTab(string title, Control content)
        {
            var page = new TabPage(title) { Padding = new Padding(6), UseVisualStyleBackColor = true };
            page.Controls.Add(content);
            _tabs.TabPages.Add(page);
        }

        // --- Liste des rapports ---------------------------------------------------------------------

        /// <summary>
        /// Relit les dossiers. Sans <paramref name="force"/>, rien n'est fait si aucun fichier n'a changé
        /// (la sélection et le défilement restent tels quels). Les rapports déjà lus et non modifiés
        /// depuis ne sont jamais relus.
        /// </summary>
        public void RefreshReports(bool force)
        {
            IList<string> folders = _folders();
            var files = new List<FileInfo>();
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (string folder in folders)
            {
                try
                {
                    if (!Directory.Exists(folder)) continue;
                    foreach (FileInfo file in new DirectoryInfo(folder).GetFiles("*.txt"))
                        if (seen.Add(file.FullName)) files.Add(file);
                }
                catch (Exception)
                {
                    // dossier devenu inaccessible : ignoré
                }
            }
            string signature = string.Join("|", folders) + "#" +
                string.Join("|", files.Select(f => f.FullName + "*" + f.LastWriteTimeUtc.Ticks.ToString(CultureInfo.InvariantCulture) + "*" +
                                                   f.Length.ToString(CultureInfo.InvariantCulture)));
            if (!force && signature == _signature) return;
            _signature = signature;

            var reports = new List<SessionReport>();
            foreach (FileInfo file in files)
            {
                string path = file.FullName;
                if (!_cache.TryGetValue(path, out SessionReport report) || report.FileTime != file.LastWriteTime)
                {
                    report = SessionReport.Load(path);
                    // Fichier verrouillé ou disparu entre-temps : ni affiché ni mémorisé, il sera relu la prochaine fois.
                    if (report.LoadFailed)
                    {
                        _cache.Remove(path);
                        _signature = null;
                        continue;
                    }
                    _cache[path] = report;
                }
                reports.Add(report);
            }
            foreach (string gone in _cache.Keys.Where(k => !seen.Contains(k)).ToList()) _cache.Remove(gone);
            _reports = reports.OrderByDescending(r => r.Start ?? r.FileTime).ToList();

            // Filtre par jeu : la sélection en cours est gardée si le jeu existe encore.
            string current = _gameFilter.SelectedItem as string ?? AllGames;
            _filling = true;
            try
            {
                _gameFilter.Items.Clear();
                _gameFilter.Items.Add(AllGames);
                foreach (string game in _reports.Select(r => r.Game).Distinct(StringComparer.OrdinalIgnoreCase).OrderBy(g => g, StringComparer.OrdinalIgnoreCase))
                    _gameFilter.Items.Add(game);
                int index = 0;
                for (int i = 1; i < _gameFilter.Items.Count; i++)
                    if (string.Equals((string)_gameFilter.Items[i], current, StringComparison.OrdinalIgnoreCase)) index = i;
                _gameFilter.SelectedIndex = index;
            }
            finally
            {
                _filling = false;
            }
            FillList();

            _status.Text = folders.Count == 0
                ? "Aucun dossier de rapports trouvé."
                : string.Format(CultureInfo.InvariantCulture, "{0} rapport(s)  -  dossier : {1}", _reports.Count, string.Join("  |  ", folders));
        }

        private static string Number(double value, string format)
        {
            return double.IsNaN(value) ? "--" : value.ToString(format, CultureInfo.InvariantCulture);
        }

        private void FillList()
        {
            var selected = new HashSet<string>(_list.SelectedItems.Cast<ListViewItem>().Select(i => ((SessionReport)i.Tag).Path), StringComparer.OrdinalIgnoreCase);
            string game = _gameFilter.SelectedItem as string ?? AllGames;
            _filling = true; // la sélection change plusieurs fois ci-dessous : une seule mise à jour à la fin
            _list.BeginUpdate();
            try
            {
                _list.Items.Clear();
                foreach (SessionReport r in _reports)
                {
                    if (game != AllGames && !string.Equals(r.Game, game, StringComparison.OrdinalIgnoreCase)) continue;
                    DateTime when = r.Start ?? r.FileTime;
                    var item = new ListViewItem(when.ToString("yyyy-MM-dd HH:mm", CultureInfo.InvariantCulture)) { Tag = r };
                    item.SubItems.Add(r.Game);
                    item.SubItems.Add(r.DurationShort);
                    item.SubItems.Add(Number(r.FpsAvg, "0.0"));
                    item.SubItems.Add(Number(r.Low1, "0.0"));
                    item.SubItems.Add(double.IsNaN(r.TargetHeld) ? "--" : Number(r.TargetHeld, "0") + " %");
                    if (!r.Readable) item.ForeColor = SystemColors.GrayText;
                    else if (!r.Finished) item.ForeColor = Color.DarkGoldenrod; // session en cours ou jeu fermé brutalement
                    _list.Items.Add(item);
                    if (selected.Contains(r.Path)) item.Selected = true;
                }
                if (_list.SelectedItems.Count == 0 && _list.Items.Count > 0) _list.Items[0].Selected = true;
            }
            finally
            {
                _list.EndUpdate();
                _filling = false;
            }
            if (_list.SelectedItems.Count > 0) _list.SelectedItems[0].EnsureVisible();
            ShowSelection();
        }

        private List<SessionReport> Selected()
        {
            return _list.SelectedItems.Cast<ListViewItem>().Select(i => (SessionReport)i.Tag).ToList();
        }

        // Session précédente du même jeu (la plus récente commencée avant celle-ci).
        private SessionReport PreviousOf(SessionReport report)
        {
            DateTime when = report.Start ?? report.FileTime;
            return _reports
                .Where(r => r != report && r.Readable && string.Equals(r.Game, report.Game, StringComparison.OrdinalIgnoreCase) && (r.Start ?? r.FileTime) < when)
                .OrderByDescending(r => r.Start ?? r.FileTime)
                .FirstOrDefault();
        }

        private void ShowSelection()
        {
            List<SessionReport> selected = Selected();
            bool any = selected.Count > 0;
            _delete.Enabled = any;
            _notepad.Enabled = any;
            SessionReport report = any ? selected[0] : null;
            ShowAnalysis(report);
            _chart.Report = report;
            _raw.Text = report != null ? report.Text.TrimStart('﻿') : "";
            SessionReport other = selected.Count >= 2 ? selected[1] : (report != null ? PreviousOf(report) : null);
            ShowComparison(report, other, selected.Count >= 2);
        }

        // --- Onglet Analyse ---------------------------------------------------------------------------

        private void Write(string text, Font font, Color color)
        {
            _analysis.SelectionStart = _analysis.TextLength;
            _analysis.SelectionLength = 0;
            _analysis.SelectionFont = font;
            _analysis.SelectionColor = color;
            _analysis.SelectedText = text;
        }

        private void ShowAnalysis(SessionReport r)
        {
            _analysis.Clear();
            if (r == null)
            {
                _analysis.Text = _reports.Count == 0
                    ? "Aucun rapport pour l'instant. Un rapport est écrit pour chaque session de jeu de plus de 15 secondes, " +
                      "si l'option est active (Réglages, onglet « Mesures et rapports »)."
                    : "Sélectionnez un rapport dans la liste.";
                return;
            }

            if (_simple.Checked)
            {
                ShowSimple(r);
                return;
            }

            Font regular = _regular, bold = _bold, title = _title, mono = _mono;
            Color text = SystemColors.WindowText, muted = SystemColors.GrayText;

            DateTime when = r.Start ?? r.FileTime;
            Write(r.Game + "\n", title, text);
            Write(when.ToString("dddd d MMMM yyyy, HH:mm", CultureInfo.GetCultureInfo("fr-FR")) +
                  (r.Finished ? "" : "   (session en cours, ou jeu fermé sans terminer sa session)") + "\n", regular, muted);
            var context = new List<string>();
            if (r.Headset.Length > 0 && r.Headset != "--") context.Add(r.Headset);
            if (r.Runtime.Length > 0 && r.Runtime != "--") context.Add(r.Runtime);
            if (r.Render.Length > 0 && r.Render != "--")
            {
                double scale = ReportAnalysis.RenderScale(r);
                context.Add(!double.IsNaN(scale) && Math.Abs(scale - 1.0) >= 0.03
                    ? string.Format(CultureInfo.InvariantCulture, "rendu {0} par œil ({1} % des {2} recommandés)", r.Render, (100.0 * scale).ToString("0", CultureInfo.InvariantCulture), r.RenderRecommended)
                    : "rendu " + r.Render + " par œil");
            }
            if (r.Api.Length > 0) context.Add(r.Api);
            if (r.DurationText.Length > 0) context.Add(r.DurationText);
            Write(string.Join("  ·  ", context) + "\n", regular, muted);
            var hardware = new List<string>();
            if (r.Cpu.Length > 0) hardware.Add(r.Cpu);
            if (r.Gpu.Length > 0) hardware.Add(r.Gpu);
            if (hardware.Count > 0) Write(string.Join("  ·  ", hardware) + "\n", regular, muted);
            Write("\n", regular, muted);

            Write("Constats\n", bold, text);
            foreach (Finding finding in ReportAnalysis.Analyze(r, PreviousOf(r)))
            {
                Color color = finding.Level == FindingLevel.Good ? Color.ForestGreen
                            : finding.Level == FindingLevel.Warning ? Color.DarkOrange
                            : finding.Level == FindingLevel.Problem ? Color.Firebrick
                            : text;
                Write("  ●  ", bold, color);
                Write(finding.Text + "\n", regular, text);
            }

            if (!r.Readable) return;
            Write("\nImages par seconde\n", bold, text);
            Write(string.Format(CultureInfo.InvariantCulture, "  moyenne {0}   min {1}   max {2}   1 % low {3}   0,1 % low {4}{5}\n",
                Number(r.FpsAvg, "0.0"), Number(r.FpsMin, "0"), Number(r.FpsMax, "0"), Number(r.Low1, "0.0"), Number(r.Low01, "0.0"),
                double.IsNaN(r.OutFps) ? "" : "   avec framegen " + Number(r.OutFps, "0.0")), regular, text);

            Write("\nTemps par image (ms, moyenne / max)\n", bold, text);
            Write(string.Format(CultureInfo.InvariantCulture, "  image {0} / {1}   CPU {2} / {3}   GPU {4} {5} / {6}{7}\n",
                Number(r.FrameAvg, "0.0"), Number(r.FrameMax, "0.0"), Number(r.CpuMsAvg, "0.0"), Number(r.CpuMsMax, "0.0"),
                r.GpuIsMeasured ? "mesuré" : "estimé", Number(r.GpuIsMeasured ? r.GpuMeasuredAvg : r.GpuEstimateAvg, "0.0"),
                Number(r.GpuIsMeasured ? r.GpuMeasuredMax : r.GpuEstimateMax, "0.0"),
                r.GpuIsMeasured ? "   (estimé " + Number(r.GpuEstimateAvg, "0.0") + ")"
                : r.GpuMeasureImplausible ? "   (mesure de " + Number(r.GpuMeasuredAvg, "0.0") + " ms écartée : non représentative)" : ""), regular, text);

            if (!double.IsNaN(r.Distribution[0]))
            {
                Write(string.Format(CultureInfo.InvariantCulture, "\nRépartition des temps d'image (budget {0} ms par image)\n", Number(r.BudgetMs, "0.0")), bold, text);
                string[] labels = { "dans le budget", "jusqu'à 1,5 x", "de 1,5 à 2 x", "plus de 2 x" };
                Color[] colors = { Color.ForestGreen, Color.Goldenrod, Color.DarkOrange, Color.Firebrick };
                for (int i = 0; i < 4; i++)
                {
                    double share = double.IsNaN(r.Distribution[i]) ? 0 : r.Distribution[i];
                    int blocks = (int)Math.Round(share / 100.0 * 40);
                    Write("  " + labels[i].PadRight(16), mono, text);
                    Write(new string('█', blocks), mono, colors[i]);
                    Write(new string('░', 40 - blocks), mono, Color.Gainsboro);
                    Write(string.Format(CultureInfo.InvariantCulture, " {0,5} %\n", Number(r.Distribution[i], "0.0")), mono, text);
                }
            }

            Write("\nProcesseur et carte graphique (moyenne / max)\n", bold, text);
            Write(string.Format(CultureInfo.InvariantCulture, "  CPU : charge {0} / {1} %   température {2} / {3} °C   fréquence {4} MHz\n",
                Number(r.CpuLoadAvg, "0"), Number(r.CpuLoadMax, "0"), Number(r.CpuTempAvg, "0"), Number(r.CpuTempMax, "0"), Number(r.CpuMHzAvg, "0")), regular, text);
            Write(string.Format(CultureInfo.InvariantCulture, "  GPU : charge {0} / {1} %   température {2} / {3} °C   fréquence {4} MHz\n",
                Number(r.GpuLoadAvg, "0"), Number(r.GpuLoadMax, "0"), Number(r.GpuTempAvg, "0"), Number(r.GpuTempMax, "0"), Number(r.GpuMHzAvg, "0")), regular, text);
            Write(string.Format(CultureInfo.InvariantCulture, "  VRAM : {0} / {1} Go sur {2}\n", Number(r.VramAvg, "0.0"), Number(r.VramMax, "0.0"), Number(r.VramTotal, "0.0")),
                regular, text);
            Write("\nLes constats sont tirés de moyennes et de maximums : ce sont des indices, pas un diagnostic.\n", regular, muted);
            _analysis.SelectionStart = 0;
        }

        // Lecture simple : verdict, explications en phrases, pistes concrètes, petit lexique.
        private void ShowSimple(SessionReport r)
        {
            Color text = SystemColors.WindowText, muted = SystemColors.GrayText;
            DateTime when = r.Start ?? r.FileTime;
            Write(r.Game + "\n", _title, text);
            Write(when.ToString("dddd d MMMM yyyy, HH:mm", CultureInfo.GetCultureInfo("fr-FR")) +
                  (string.IsNullOrEmpty(r.DurationText) ? "" : "   ·   partie de " + r.DurationShort) + "\n\n", _regular, muted);

            NoviceView view = NoviceSummary.Build(r, PreviousOf(r));
            Color verdictColor = view.Level == FindingLevel.Good ? Color.ForestGreen
                               : view.Level == FindingLevel.Warning ? Color.DarkOrange
                               : view.Level == FindingLevel.Problem ? Color.Firebrick
                               : text;
            Write("●  ", _verdict, verdictColor);
            Write(view.Verdict + "\n", _verdict, verdictColor);
            Write(view.Summary + "\n", _regular, text);

            foreach (NoviceSection section in view.Sections)
            {
                Write("\n" + section.Title + "\n", _bold, text);
                foreach (string line in section.Lines) Write(line + "\n", _regular, text);
            }
            if (view.Tips.Count > 0)
            {
                Write("\nQue pouvez-vous essayer ?\n", _bold, text);
                foreach (string tip in view.Tips)
                {
                    Write("  →  ", _bold, Color.SteelBlue);
                    Write(tip + "\n", _regular, text);
                }
            }

            Write("\nPetit lexique\n", _bold, muted);
            foreach (string[] entry in NoviceSummary.Glossary)
            {
                Write(entry[0] + " : ", _bold, muted);
                Write(entry[1] + "\n", _regular, muted);
            }
            Write("\nCes explications sont tirées des moyennes de la partie : ce sont des pistes, pas un diagnostic. " +
                  "Tous les chiffres sont dans la lecture « Détaillé ».\n", _regular, muted);
            _analysis.SelectionStart = 0;
        }

        // --- Onglet Comparaison -------------------------------------------------------------------------

        private void ShowComparison(SessionReport a, SessionReport b, bool chosen)
        {
            _compare.BeginUpdate();
            try
            {
                _compare.Items.Clear();
                _compareHint.Text = a == null ? ""
                    : b == null ? "Sélectionnez deux rapports (Ctrl + clic) pour les comparer."
                    : chosen ? "A = premier rapport sélectionné, B = second."
                    : "B = session précédente du même jeu. Ctrl + clic dans la liste pour en choisir une autre.";
                string hardware = a != null && b != null ? ReportAnalysis.HardwareDifference(a, b) : "";
                string setup = a != null && b != null ? ReportAnalysis.SetupDifference(a, b) : "";
                if (setup.Length > 0) _compareHint.Text += "   Réglages différents : " + setup + ".";
                if (hardware.Length > 0) _compareHint.Text += "   Matériel différent : " + hardware + ".";
                if (a == null) return;
                _compare.Columns[1].Text = Label(a, "A");
                _compare.Columns[2].Text = b != null ? Label(b, "B") : "Session B";
                if (b == null) return;

                // (intitulé, valeur, format, plus haut = mieux ?)
                var rows = new List<Tuple<string, Func<SessionReport, double>, string, bool>>
                {
                    Tuple.Create<string, Func<SessionReport, double>, string, bool>("FPS moyen", r => r.FpsAvg, "0.0", true),
                    Tuple.Create<string, Func<SessionReport, double>, string, bool>("FPS minimum (sur 1 s)", r => r.FpsMin, "0", true),
                    Tuple.Create<string, Func<SessionReport, double>, string, bool>("1 % low", r => r.Low1, "0.0", true),
                    Tuple.Create<string, Func<SessionReport, double>, string, bool>("0,1 % low", r => r.Low01, "0.0", true),
                    Tuple.Create<string, Func<SessionReport, double>, string, bool>("Cible tenue (%)", r => r.TargetHeld, "0", true),
                    Tuple.Create<string, Func<SessionReport, double>, string, bool>("Avec framegen", r => r.OutFps, "0.0", true),
                    Tuple.Create<string, Func<SessionReport, double>, string, bool>("Temps d'image moyen (ms)", r => r.FrameAvg, "0.0", false),
                    Tuple.Create<string, Func<SessionReport, double>, string, bool>("Temps CPU moyen (ms)", r => r.CpuMsAvg, "0.0", false),
                    Tuple.Create<string, Func<SessionReport, double>, string, bool>("Temps GPU mesuré (ms)", r => r.GpuIsMeasured ? r.GpuMeasuredAvg : double.NaN, "0.0", false),
                    Tuple.Create<string, Func<SessionReport, double>, string, bool>("Temps GPU estimé (ms)", r => r.GpuEstimateAvg, "0.0", false),
                    Tuple.Create<string, Func<SessionReport, double>, string, bool>("Charge CPU moyenne (%)", r => r.CpuLoadAvg, "0", false),
                    Tuple.Create<string, Func<SessionReport, double>, string, bool>("Charge GPU moyenne (%)", r => r.GpuLoadAvg, "0", false),
                    Tuple.Create<string, Func<SessionReport, double>, string, bool>("Température CPU max (°C)", r => r.CpuTempMax, "0", false),
                    Tuple.Create<string, Func<SessionReport, double>, string, bool>("Température GPU max (°C)", r => r.GpuTempMax, "0", false),
                    Tuple.Create<string, Func<SessionReport, double>, string, bool>("VRAM max (Go)", r => r.VramMax, "0.0", false),
                };
                foreach (var row in rows)
                {
                    double va = row.Item2(a), vb = row.Item2(b);
                    if (double.IsNaN(va) && double.IsNaN(vb)) continue;
                    var item = new ListViewItem(row.Item1) { UseItemStyleForSubItems = false };
                    item.SubItems.Add(Number(va, row.Item3));
                    item.SubItems.Add(Number(vb, row.Item3));
                    double delta = va - vb;
                    var deltaItem = item.SubItems.Add(double.IsNaN(delta) ? "--" : (delta > 0 ? "+" : "") + delta.ToString(row.Item3, CultureInfo.InvariantCulture));
                    if (!double.IsNaN(delta) && Math.Abs(delta) > 1e-9)
                    {
                        bool better = row.Item4 ? delta > 0 : delta < 0;
                        deltaItem.ForeColor = better ? Color.ForestGreen : Color.Firebrick;
                    }
                    _compare.Items.Add(item);
                }
            }
            finally
            {
                _compare.EndUpdate();
            }
        }

        private static string Label(SessionReport r, string letter)
        {
            DateTime when = r.Start ?? r.FileTime;
            return letter + " : " + when.ToString("dd/MM HH:mm", CultureInfo.InvariantCulture);
        }

        // --- Actions ------------------------------------------------------------------------------------

        private void OpenSelectedInNotepad()
        {
            foreach (SessionReport r in Selected().Take(5))
            {
                try
                {
                    Process.Start(new ProcessStartInfo("notepad.exe", "\"" + r.Path + "\"") { UseShellExecute = true });
                }
                catch (Exception ex)
                {
                    MessageBox.Show(this, "Impossible d'ouvrir le Bloc-notes : " + ex.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }
            }
        }

        private void OpenFolder()
        {
            try
            {
                List<SessionReport> selected = Selected();
                if (selected.Count > 0)
                {
                    Process.Start(new ProcessStartInfo("explorer.exe", "/select,\"" + selected[0].Path + "\"") { UseShellExecute = true });
                    return;
                }
                string folder = _folders().FirstOrDefault(Directory.Exists);
                if (folder != null) Process.Start(new ProcessStartInfo("explorer.exe", "\"" + folder + "\"") { UseShellExecute = true });
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, "Impossible d'ouvrir le dossier : " + ex.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        }

        // Les rapports supprimés vont dans la corbeille : une erreur de sélection se rattrape. Exception
        // de Windows : sur un disque sans corbeille (lecteur réseau, clé USB), la suppression est définitive.
        private void DeleteSelected()
        {
            List<SessionReport> selected = Selected();
            if (selected.Count == 0) return;
            string question = selected.Count == 1
                ? "Envoyer ce rapport à la corbeille ?\n\n" + Path.GetFileName(selected[0].Path)
                : string.Format(CultureInfo.InvariantCulture, "Envoyer ces {0} rapports à la corbeille ?", selected.Count);
            if (MessageBox.Show(this, question, Text, MessageBoxButtons.YesNo, MessageBoxIcon.Question, MessageBoxDefaultButton.Button2) != DialogResult.Yes)
                return;

            var failures = new List<string>();
            foreach (SessionReport r in selected)
            {
                try
                {
                    Microsoft.VisualBasic.FileIO.FileSystem.DeleteFile(r.Path, Microsoft.VisualBasic.FileIO.UIOption.OnlyErrorDialogs,
                        Microsoft.VisualBasic.FileIO.RecycleOption.SendToRecycleBin);
                }
                catch (Exception ex)
                {
                    failures.Add(Path.GetFileName(r.Path) + " : " + ex.Message);
                }
            }
            RefreshReports(true);
            if (failures.Count > 0)
                MessageBox.Show(this, "Certains rapports n'ont pas pu être supprimés :\n\n" + string.Join("\n", failures), Text, MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }

        // Fermer la fenêtre la masque seulement : le programme continue dans la zone de notification.
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
                _regular.Dispose();
                _bold.Dispose();
                _title.Dispose();
                _mono.Dispose();
                _verdict.Dispose();
            }
            base.Dispose(disposing);
        }
    }

    /// <summary>Courbes du relevé minute par minute d'un rapport : cadence, temps par image, températures.</summary>
    internal sealed class ReportChart : Control
    {
        private SessionReport _report;

        public ReportChart()
        {
            SetStyle(ControlStyles.OptimizedDoubleBuffer | ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
            BackColor = SystemColors.Window;
        }

        public SessionReport Report
        {
            get { return _report; }
            set
            {
                _report = value;
                Invalidate();
            }
        }

        private sealed class Series
        {
            public string Name;
            public Color Color;
            public Func<MinuteRow, double> Value;
            public bool Dashed;
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.Clear(BackColor);
            g.SmoothingMode = SmoothingMode.AntiAlias;
            SessionReport r = _report;
            List<MinuteRow> rows = r != null ? r.Minutes : null;
            if (rows == null || rows.Count < 2)
            {
                string message = r == null ? "" : "Pas de relevé minute par minute dans ce rapport : session de moins de deux minutes, ou rapport d'une version précédente.";
                TextRenderer.DrawText(g, message, Font, ClientRectangle, SystemColors.GrayText,
                    TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.WordBreak);
                return;
            }

            double budget = r.Target > 0 ? 1000.0 / r.Target : double.NaN;
            int height = (ClientSize.Height - 20) / 3;
            var area = new Rectangle(8, 8, ClientSize.Width - 16, height - 8);
            DrawPanel(g, area, rows, "Images par seconde", 0, Math.Max(double.IsNaN(r.Target) ? 0 : r.Target, rows.Max(m => Value(m.FpsAvg))) * 1.15,
                r.Target, "cible", new[]
                {
                    new Series { Name = "moyenne", Color = Color.RoyalBlue, Value = m => m.FpsAvg },
                    new Series { Name = "minimum (1 s)", Color = Color.SkyBlue, Value = m => m.FpsMin },
                });
            area.Offset(0, height);
            double frameTop = Math.Max(double.IsNaN(budget) ? 0 : budget * 1.5, Math.Max(rows.Max(m => Value(m.CpuMs)), rows.Max(m => Value(m.GpuMs))) * 1.15);
            DrawPanel(g, area, rows, "Temps par image (ms)", 0, frameTop, budget, "budget", new[]
            {
                new Series { Name = "CPU", Color = Color.DimGray, Value = m => m.CpuMs },
                // Rapport ancien avec une mesure invraisemblable : la courbe GPU (cette mesure) est retirée.
                r.GpuMeasureImplausible
                    ? new Series { Name = "GPU (mesure non représentative, retirée)", Color = Color.Silver, Value = m => double.NaN }
                    : new Series { Name = r.MinutesGpuMeasured ? "GPU (mesuré)" : "GPU (estimé)", Color = Color.SeaGreen, Value = m => m.GpuMs, Dashed = !r.MinutesGpuMeasured },
            });
            area.Offset(0, height);
            DrawPanel(g, area, rows, "Températures (°C) et charge GPU (%)", 0, 100, double.NaN, "", new[]
            {
                new Series { Name = "CPU °C", Color = Color.DarkOrange, Value = m => m.CpuTemp },
                new Series { Name = "GPU °C", Color = Color.Firebrick, Value = m => m.GpuTemp },
                new Series { Name = "charge GPU %", Color = Color.MediumPurple, Value = m => m.GpuLoad, Dashed = true },
            });
        }

        private static double Value(double v)
        {
            return double.IsNaN(v) ? 0 : v;
        }

        private void DrawPanel(Graphics g, Rectangle area, List<MinuteRow> rows, string title, double min, double max, double reference, string referenceName,
            Series[] series)
        {
            if (!(max > min)) max = min + 1;
            using (var titleFont = new Font(Font, FontStyle.Bold))
            {
                TextRenderer.DrawText(g, title, titleFont, new Point(area.Left, area.Top), ForeColor);
            }
            var plot = Rectangle.FromLTRB(area.Left + 44, area.Top + 22, area.Right - 8, area.Bottom - 18);
            if (plot.Width < 40 || plot.Height < 20) return;

            // Graduations horizontales.
            using (var grid = new Pen(Color.Gainsboro))
            {
                for (int i = 0; i <= 4; i++)
                {
                    double value = min + (max - min) * i / 4;
                    int y = plot.Bottom - (int)Math.Round(plot.Height * (double)i / 4);
                    g.DrawLine(grid, plot.Left, y, plot.Right, y);
                    TextRenderer.DrawText(g, value.ToString(max - min >= 20 ? "0" : "0.0", CultureInfo.InvariantCulture), Font,
                        new Rectangle(area.Left, y - 8, 40, 16), SystemColors.GrayText, TextFormatFlags.Right | TextFormatFlags.VerticalCenter);
                }
            }
            // Minutes en abscisse : une étiquette toutes les 1, 5, 10, 15 ou 30 minutes selon la longueur.
            int count = rows.Count;
            int step = count <= 12 ? 1 : count <= 60 ? 5 : count <= 120 ? 10 : count <= 240 ? 15 : 30;
            Func<int, int> xOf = i => plot.Left + (int)Math.Round(plot.Width * (count == 1 ? 0.5 : (double)i / (count - 1)));
            for (int i = 0; i < count; i++)
            {
                if (rows[i].Minute % step != 0 && i != 0) continue;
                TextRenderer.DrawText(g, rows[i].Minute + " min", Font, new Point(xOf(i) - 14, plot.Bottom + 2), SystemColors.GrayText);
            }

            Func<double, int> yOf = v => plot.Bottom - (int)Math.Round(plot.Height * Math.Min(1.0, Math.Max(0.0, (v - min) / (max - min))));
            if (!double.IsNaN(reference) && reference > min && reference < max)
            {
                using (var pen = new Pen(Color.Gray) { DashStyle = DashStyle.Dash })
                {
                    int y = yOf(reference);
                    g.DrawLine(pen, plot.Left, y, plot.Right, y);
                    TextRenderer.DrawText(g, referenceName + " " + reference.ToString("0.0", CultureInfo.InvariantCulture), Font,
                        new Rectangle(plot.Right - 120, y - 16, 120, 15), Color.Gray, TextFormatFlags.Right);
                }
            }

            int legendX = area.Left + TextRenderer.MeasureText(title, Font).Width + 40;
            foreach (Series s in series)
            {
                using (var pen = new Pen(s.Color, 2f) { DashStyle = s.Dashed ? DashStyle.Dash : DashStyle.Solid })
                {
                    // Courbe, interrompue là où la valeur manque.
                    var points = new List<Point>();
                    for (int i = 0; i <= count; i++)
                    {
                        double v = i < count ? s.Value(rows[i]) : double.NaN;
                        if (!double.IsNaN(v))
                        {
                            points.Add(new Point(xOf(i), yOf(v)));
                            continue;
                        }
                        if (points.Count >= 2) g.DrawLines(pen, points.ToArray());
                        else if (points.Count == 1) g.DrawEllipse(pen, points[0].X - 1, points[0].Y - 1, 2, 2);
                        points.Clear();
                    }
                    // Légende.
                    g.DrawLine(pen, legendX, area.Top + 8, legendX + 18, area.Top + 8);
                }
                TextRenderer.DrawText(g, s.Name, Font, new Point(legendX + 22, area.Top + 1), ForeColor);
                legendX += 22 + TextRenderer.MeasureText(s.Name, Font).Width + 18;
            }
        }
    }
}
