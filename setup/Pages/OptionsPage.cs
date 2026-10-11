using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Windows.Forms;

namespace NeoXR.Setup
{
    /// <summary>Additional runtime settings, with edits staged immediately for the shared preview.</summary>
    sealed class OptionsPage : WizardPage
    {
        readonly List<Action> loaders = new List<Action>();
        readonly TableLayoutPanel grid = Ui.Grid(2);
        bool loading;

        public OptionsPage(SetupContext context, bool placement) : base(context,
            placement ? "Wheel & placement" : "Overview",
            placement ? "Choose a wheel file and tune its motion and placement. Changes reload live in VR." : "Configure your wheel, then Apply live. Minimize to keep running; close to exit.")
        {
            if (!placement) {
                Flag("Enabled", "Show the wheel in VR", false);
                TextSetting("Process", "Game executable (next game launch)", "iRacingSim64DX11.exe");
                Flag("Passthrough", "Allow headset passthrough in edit mode", true);
                Ui.Stack(this, Ui.Paragraph("Welcome to NeoXR"),
                    Ui.Paragraph("Choose a section on the left to configure your wheel. Bind controls by pressing them on your hardware and check their movement in the live preview."), grid,
                    Ui.Paragraph("IN THE HEADSET\nF8  •  Calibrate position\nF9  •  Hide / show the wheel\nTab  •  Enter placement mode (customizable in Shortcuts)\nM  •  Switch size / rotation mode\nH  •  Toggle passthrough while editing\nEsc, then Enter  •  Save placement"),
                    Ui.Paragraph("SETTINGS FILE\n" + Settings.Path),
                    Ui.Paragraph("Apply live saves your settings and updates the running game. Closing exits this app and discards unapplied edits. Model files and their textures reload automatically after you save them in your modelling tool. Device numbers can change when hardware is reconnected."));
            } else {
                var model = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 240 };
                model.Items.AddRange(new object[] { "custom", "placeholder" });
                loaders.Add(() => { var value = Settings.Text("Model", "custom"); if (!model.Items.Contains(value)) model.Items.Add(value); model.SelectedItem = value; });
                model.SelectedIndexChanged += (s, e) => { if (!loading) Settings.Set("Model", model.Text); };
                Ui.AddRow(grid, Ui.Text("Wheel model"), model);
                var file = new TextBox { Width = 250 };
                loaders.Add(() => file.Text = Settings.Text("ModelFile", "assets/neo-x.gltf"));
                file.TextChanged += (s, e) => { if (!loading) Settings.Set("ModelFile", file.Text.Trim()); };
                var row = new FlowLayoutPanel { AutoSize = true, WrapContents = false };
                row.Controls.Add(file);
                row.Controls.Add(Ui.Button("Browse…", (s, e) => {
                    using (var dialog = new OpenFileDialog { Filter = "Wheel models|*.fbx;*.obj;*.gltf;*.glb", CheckFileExists = true })
                        if (dialog.ShowDialog(this) == DialogResult.OK) { file.Text = dialog.FileName; model.SelectedItem = "custom"; }
                }));
                Ui.AddRow(grid, Ui.Text("Wheel file"), row);
                Number("WidthMm", "Wheel width (mm)", 316, 10, 2000, 1, 1);
                Number("X", "Left / right (metres)", 0, -100, 100, 3, .01m);
                Number("Y", "Up / down (metres)", -.3m, -100, 100, 3, .01m);
                Number("Z", "Depth (metres; negative = forward)", -.5m, -100, 100, 3, .01m);
                Number("TiltDegrees", "Tilt (degrees)", 0, -90, 90, 1, 1);
                Number("YawDegrees", "Heading (degrees)", 0, -90, 90, 1, 1);
                Number("EditOpacity", "Opacity while editing (0.1–1)", .5m, .1m, 1, 2, .05m);
                Number("ButtonTravelMm", "Button travel (mm)", 1.2m, 0, 5, 1, .1m);
                Number("PaddleAngleDegrees", "Shifter angle (degrees)", 7, 0, 25, 1, 1);
                Number("ClutchAngleDegrees", "Clutch angle (degrees)", 12, 0, 40, 1, 1);
                Number("KnobStepDegrees", "Knob step (degrees)", 15, 0, 90, 1, 1);
                Number("StickAngleDegrees", "Joystick tilt (degrees)", 12, 0, 30, 1, 1);
                Number("LeftClutchButton", "Left digital clutch button (-1 = off)", -1, -1, 127, 0, 1);
                Number("RightClutchButton", "Right digital clutch button (-1 = off)", -1, -1, 127, 0, 1);
                TextSetting("ClickSoundFile", "Custom click WAV (blank = built-in)", "");
                Ui.Stack(this, Ui.Paragraph("Apply live updates general placement and any changed placement fields in the current car's saved alignment. In VR, align each car with the edit key, then Esc and Enter. The desktop preview shows tilt, heading, appearance and animation; position is visible in VR."), grid,
                    Ui.Button("Reset general calibration", (s, e) => { Settings.Set("Calibration", ""); MessageBox.Show(this, "Calibration reset is staged. Apply live, then press F8 in VR to calibrate again.", "NeoXR"); }));
            }
        }

        protected override void Entering() { loading = true; try { foreach (var load in loaders) load(); } finally { loading = false; } }

        void Flag(string key, string label, bool fallback)
        {
            var field = new CheckBox { Text = label, AutoSize = true };
            loaders.Add(() => field.Checked = Settings.Flag(key, fallback));
            field.CheckedChanged += (s, e) => { if (!loading) Settings.Set(key, field.Checked); };
            Ui.AddRow(grid, field); grid.SetColumnSpan(field, 2);
        }

        void TextSetting(string key, string label, string fallback)
        {
            var field = new TextBox { Width = 250 };
            loaders.Add(() => field.Text = Settings.Text(key, fallback));
            field.TextChanged += (s, e) => { if (!loading) Settings.Set(key, field.Text.Trim()); };
            Ui.AddRow(grid, Ui.Text(label), field);
        }

        void Number(string key, string label, decimal fallback, decimal min, decimal max, int digits, decimal increment)
        {
            var field = new NumericUpDown { Width = 140, Minimum = min, Maximum = max, DecimalPlaces = digits, Increment = increment };
            loaders.Add(() => {
                decimal value;
                if (!decimal.TryParse(Settings.Text(key), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out value)) value = fallback;
                field.Value = Math.Max(min, Math.Min(max, value));
            });
            field.ValueChanged += (s, e) => { if (!loading) Settings.Set(key, (float)field.Value, digits == 0 ? "0" : "0." + new string('#', digits)); };
            Ui.AddRow(grid, Ui.Text(label), field);
        }
    }

    sealed class DiagnosticsPage : WizardPage
    {
        readonly TextBox log = new TextBox { Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Both, WordWrap = false, Height = 340, Font = new Font("Consolas", 9) };
        readonly Label info = Ui.Paragraph("");

        public DiagnosticsPage(SetupContext context) : base(context, "Diagnostics", "Inspect the runtime log, saved car alignments and installation files.")
        {
            var actions = new FlowLayoutPanel { AutoSize = true };
            actions.Controls.Add(Ui.Button("Refresh log", (s, e) => RefreshLog()));
            actions.Controls.Add(Ui.Button("Open installation folder", (s, e) => Open(Path.GetDirectoryName(Settings.Path))));
            actions.Controls.Add(Ui.Button("Open settings file", (s, e) => Open(Settings.Path)));
            Ui.Stack(this, info, actions, log, Ui.Paragraph("CAR ALIGNMENTS\nPer-car positions are stored in the [Cars] section of NeoXR.ini. To remove the current car's alignment, enter edit mode in VR, then press Esc and Delete. Live placement edits update changed fields in the active car alignment; other car alignments are preserved."));
        }
        protected override void Entering() => RefreshLog();
        void RefreshLog()
        {
            var folder = Path.GetDirectoryName(Settings.Path);
            info.Text = "Installation: " + folder + "\nInput bridge: " + (File.Exists(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "NeoXR-InputBridge.dll")) ? "Present" : "Missing — live input / preview unavailable") + "\nLog: NeoXR.log";
            try {
                using (var stream = new FileStream(Path.Combine(folder, "NeoXR.log"), FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete)) {
                    bool tail = stream.Length > 65536;
                    if (tail) stream.Seek(-65536, SeekOrigin.End);
                    using (var reader = new StreamReader(stream)) { if (tail) reader.ReadLine(); log.Text = reader.ReadToEnd(); }
                }
            } catch (FileNotFoundException) { log.Text = "No log yet. Launch the game with NeoXR installed to create one."; }
            catch (Exception ex) { log.Text = "Could not read the log: " + ex.Message; }
        }
        void Open(string path) { try { Process.Start(new ProcessStartInfo(path) { UseShellExecute = true }); } catch (Exception ex) { MessageBox.Show(this, ex.Message, "NeoXR"); } }
    }
}
