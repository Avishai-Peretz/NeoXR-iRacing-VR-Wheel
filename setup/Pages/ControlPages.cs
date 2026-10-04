using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows.Forms;

namespace NeoXR.Setup
{
    /// <summary>A page of press-to-bind rows. Buttons, knobs and joysticks differ only in their row list.</summary>
    sealed class BindingPage : WizardPage
    {
        readonly BindingGroup group = new BindingGroup();
        readonly Label pressed = Ui.Text("");

        public BindingPage(SetupContext context, string title, string description, string help,
                           IEnumerable<(string key, string label, BindingKind kind)> rows)
            : base(context, title, description)
        {
            var grid = Ui.Grid(4);
            foreach (var (key, label, kind) in rows)
            {
                if (key == null) { Ui.AddRow(grid, Ui.Text(label, bold: true)); continue; }
                var row = group.Add(new BindingRow(key, label, kind));
                Ui.AddRow(grid, row.Name, row.Value, row.Bind, row.Clear);
            }
            Ui.Stack(this, Ui.Paragraph(help), grid, pressed);
            group.Changed += Edited;
        }

        protected override void Entering() => group.Load(Settings);
        public override void Store() => group.Store(Settings);
        public override void Leaving() { group.Stop(); Store(); }
        public override int Highlight => group.Highlight;
        public override string PreviewHint => group.Listening
            ? "Press the glowing control on your wheel."
            : "Point at a row to see where that control is. Press a control to check that the right part moves.";

        public override void OnInput(Snapshot input)
        {
            group.OnInput(input.Buttons);
            var held = input.Buttons?.PressedButtons.ToList();
            pressed.Text = held == null ? "The button device is not responding." :
                "Pressed right now: " + (held.Count > 0 ? string.Join(", ", held) : "nothing");
        }

        const string BindHelp = "Press Bind, then press the control on the wheel. A row turns green while its control is held, " +
                                "so you can check every mapping. Orange means two rows share the same button.";

        public static BindingPage Buttons(SetupContext context) => new BindingPage(context, "Buttons and shifters",
            "Map the eight backlit push buttons and the shift paddles.", BindHelp,
            Enumerable.Range(0, 8).Select(i => ($"Button{i}", $"Push button B{i + 1}", BindingKind.Button))
                .Prepend(((string)null, "Push buttons", BindingKind.Button))
                .Concat(new (string, string, BindingKind)[] {
                    (null, "Shift paddles", BindingKind.Button),
                    ("LeftPaddleButton", "Left paddle (downshift)", BindingKind.Button),
                    ("RightPaddleButton", "Right paddle (upshift)", BindingKind.Button) }));

        public static BindingPage Knobs(SetupContext context) => new BindingPage(context, "Knobs and thumb rollers",
            "Each detent sends a button press; bind one per direction.", BindHelp + " If a knob turns the wrong way in VR, swap its two bindings.",
            new (string, string, BindingKind)[] {
                (null, "Thumb rollers", BindingKind.Button),
                ("B9B10KnobCW", "B9/B10 roller, turn up", BindingKind.Button),
                ("B9B10KnobCCW", "B9/B10 roller, turn down", BindingKind.Button),
                ("B11B12KnobCW", "B11/B12 roller, turn up", BindingKind.Button),
                ("B11B12KnobCCW", "B11/B12 roller, turn down", BindingKind.Button),
                (null, "Rotary knobs", BindingKind.Button),
                ("B37B38KnobCW", "B37/B38 knob, clockwise", BindingKind.Button),
                ("B37B38KnobCCW", "B37/B38 knob, counter-clockwise", BindingKind.Button),
                ("B39B40KnobCW", "B39/B40 knob, clockwise", BindingKind.Button),
                ("B39B40KnobCCW", "B39/B40 knob, counter-clockwise", BindingKind.Button) });

        public static BindingPage Sticks(SetupContext context) => new BindingPage(context, "Joysticks",
            "Map the two thumb joysticks: four directions and the push.", BindHelp +
            " If a joystick reports as a hat switch instead of buttons, bind its \"hat\" row and leave the directions empty.",
            new[] { "Left", "Right" }.SelectMany(side => new (string, string, BindingKind)[] {
                (null, side + " joystick", BindingKind.Button),
                ($"{side}StickUp", "Up", BindingKind.Button), ($"{side}StickDown", "Down", BindingKind.Button),
                ($"{side}StickLeft", "Left", BindingKind.Button), ($"{side}StickRight", "Right", BindingKind.Button),
                ($"{side}StickPush", "Push", BindingKind.Button), ($"{side}StickPOV", "As a hat switch (optional)", BindingKind.Hat) }));
    }

    /// <summary>Both clutch levers are analog axes; each is calibrated by pulling it once.</summary>
    sealed class ClutchPage : WizardPage
    {
        sealed class Lever
        {
            const int DetectThreshold = 3000;
            readonly string prefix;
            int axis = -1;
            float rest = -10000, full = 10000;
            int[] baseline, extreme;

            public Lever(string prefix, string title)
            {
                this.prefix = prefix;
                Title = Ui.Text(title, bold: true);
                Detect = Ui.Button("Calibrate", (s, e) => Start());
                Clear = Ui.Button("Clear", (s, e) => { Cancel(); axis = -1; Show(); Changed?.Invoke(); });
            }
            public Label Title { get; }
            public Label Summary { get; } = Ui.Text("");
            public ProgressBar Position { get; } = new ProgressBar { Width = 260, Maximum = 100 };
            public Button Detect { get; }
            public Button Clear { get; }
            public bool Calibrating { get; private set; }
            public event Action Changed;

            public void Load(Settings s)
            {
                axis = s.Integer(prefix + "Axis", -1); rest = s.Real(prefix + "Rest", -10000); full = s.Real(prefix + "Full", 10000);
                Cancel(); Show();
            }
            public void Store(Settings s)
            {
                s.Set(prefix + "Axis", axis); s.Set(prefix + "Rest", rest, "0"); s.Set(prefix + "Full", full, "0");
            }
            public void CopyFrom(Lever other) { Cancel(); axis = other.axis; rest = other.rest; full = other.full; Show(); Changed?.Invoke(); }

            void Start() { Calibrating = true; baseline = null; Summary.Text = "Pull the lever all the way, then let go..."; Detect.Enabled = false; }
            public void Cancel() { Calibrating = false; Detect.Enabled = true; }

            // Released when Calibrate is pressed gives Rest; the furthest point reached gives Full.
            // Calibration finishes once the lever returns most of the way to rest.
            public void OnInput(InputState state)
            {
                if (state == null) return;
                if (Calibrating) Calibrate(state);
                float span = full - rest;
                Position.Value = axis >= 0 && Math.Abs(span) > 1 ? (int)Math.Round(100 * Math.Max(0, Math.Min(1, (state.Axis(axis) - rest) / span))) : 0;
            }

            void Calibrate(InputState state)
            {
                var now = Enumerable.Range(0, 8).Select(state.Axis).ToArray();
                if (baseline == null) { baseline = now; extreme = (int[])now.Clone(); return; }
                for (int a = 0; a < 8; a++) if (Math.Abs(now[a] - baseline[a]) > Math.Abs(extreme[a] - baseline[a])) extreme[a] = now[a];
                int best = Enumerable.Range(0, 8).OrderByDescending(a => Math.Abs(extreme[a] - baseline[a])).First();
                int travel = extreme[best] - baseline[best];
                if (Math.Abs(travel) < DetectThreshold || Math.Abs(now[best] - baseline[best]) > Math.Abs(travel) * 0.2) return;
                axis = best; rest = baseline[best]; full = extreme[best];
                Cancel(); Show(); Changed?.Invoke();
            }

            void Show() => Summary.Text = axis < 0 ? "Not set" : $"Axis {InputState.AxisNames[axis]}, released {rest:0}, pulled {full:0}";
        }

        readonly Lever left = new Lever("LeftClutch", "Left clutch lever"), right = new Lever("RightClutch", "Right clutch lever");
        readonly NumericUpDown angle = new NumericUpDown { Minimum = 0, Maximum = 40, Width = 70 };

        public ClutchPage(SetupContext context) : base(context, "Clutch levers",
            "Calibrate the analog clutch levers so they pull back in VR exactly as far as the real ones.")
        {
            var grid = Ui.Grid(3);
            foreach (var lever in new[] { left, right })
            {
                Ui.AddRow(grid, lever.Title);
                Ui.AddRow(grid, lever.Summary, lever.Detect, lever.Clear);
                Ui.AddRow(grid, lever.Position);
            }
            Ui.AddRow(grid, null, Ui.Button("Right = same as left", (s, e) => right.CopyFrom(left)));
            Ui.AddRow(grid, Ui.Text("Lever travel in VR (degrees):"), angle);
            Ui.Stack(this,
                Ui.Paragraph("With the lever released, press Calibrate, pull the lever fully and let it go. " +
                             "If both levers drive one combined axis, calibrate the left one and use \"Right = same as left\"."),
                grid);
            left.Changed += Edited; right.Changed += Edited;
            angle.ValueChanged += (s, e) => Edited();
        }

        protected override void Entering()
        {
            left.Load(Settings); right.Load(Settings);
            angle.Value = (decimal)Math.Max(0, Math.Min(40, Settings.Real("ClutchAngleDegrees", 12)));
        }
        public override void Store()
        {
            left.Store(Settings); right.Store(Settings);
            Settings.Set("ClutchAngleDegrees", (float)angle.Value, "0");
        }
        public override void Leaving() { left.Cancel(); right.Cancel(); Store(); }
        public override void OnInput(Snapshot input) { left.OnInput(input.Buttons); right.OnInput(input.Buttons); }
        public override int Highlight => left.Calibrating ? ControlSlot.LeftClutch : right.Calibrating ? ControlSlot.RightClutch : ControlSlot.None;
        public override string PreviewHint => Highlight != ControlSlot.None
            ? "Pull the glowing lever all the way, then let it go."
            : "Pull each clutch lever: the virtual one should travel exactly as far.";
    }

    /// <summary>Recenter and edit-mode controls, so one control can recenter both the game and the wheel.</summary>
    sealed class RecenterPage : WizardPage
    {
        readonly KeyBox recenterKey = new KeyBox(), editKey = new KeyBox();
        readonly BindingGroup group = new BindingGroup();
        readonly BindingRow recenterButton;
        readonly NumericUpDown sensitivity = new NumericUpDown { Minimum = 0.1m, Maximum = 10, DecimalPlaces = 1, Increment = 0.1m, Width = 70 };

        public RecenterPage(SetupContext context) : base(context, "Recenter and placement",
            "Use the same key or wheel button that recenters VR in the game, so both recenter together.")
        {
            recenterButton = group.Add(new BindingRow("RecenterButton", "Recenter wheel button:"));
            var grid = Ui.Grid(4);
            Ui.AddRow(grid, Ui.Text("Recenter key:"), recenterKey, Ui.Button("Clear", (s, e) => recenterKey.KeyName = ""));
            Ui.AddRow(grid, recenterButton.Name, recenterButton.Value, recenterButton.Bind, recenterButton.Clear);
            Ui.AddRow(grid, Ui.Text("Edit mode key:"), editKey, Ui.Button("Clear", (s, e) => editKey.KeyName = ""));
            Ui.AddRow(grid, Ui.Text("Mouse sensitivity:"), sensitivity);
            Ui.Stack(this,
                Ui.Paragraph("Click a key box and press the key. F8 always recenters as well. " +
                             "In the game, the edit key lets you drag the wheel into place with the mouse."),
                grid);
        }

        protected override void Entering()
        {
            recenterKey.KeyName = Settings.Text("RecenterKey");
            editKey.KeyName = Settings.Text("EditKey", "Tab");
            group.Load(Settings);
            sensitivity.Value = (decimal)Math.Max(0.1f, Math.Min(10, Settings.Real("EditSensitivity", 1)));
        }
        public override void Leaving() { group.Stop(); Store(); }
        public override void Store()
        {
            Settings.Set("RecenterKey", recenterKey.KeyName);
            Settings.Set("EditKey", editKey.KeyName);
            group.Store(Settings);
            Settings.Set("EditSensitivity", (float)sensitivity.Value, "0.0");
        }
        public override void OnInput(Snapshot input) => group.OnInput(input.Buttons);
    }

    /// <summary>A read-only box that shows the name of the next key pressed, in the format NeoXR.ini accepts.</summary>
    sealed class KeyBox : TextBox
    {
        public KeyBox() { ReadOnly = true; Width = 190; BackColor = System.Drawing.SystemColors.Window; }

        public string KeyName { get => Text; set => Text = value; }

        protected override void OnPreviewKeyDown(PreviewKeyDownEventArgs e) { e.IsInputKey = true; base.OnPreviewKeyDown(e); }
        protected override void OnKeyDown(KeyEventArgs e)
        {
            e.SuppressKeyPress = e.Handled = true;
            var name = IniName(e.KeyCode);
            if (name != null) KeyName = name;
        }

        // Mirrors neo::parseKey in config.hpp. Modifier keys alone are not valid bindings.
        static string IniName(Keys key)
        {
            if (key >= Keys.F1 && key <= Keys.F24) return "F" + (key - Keys.F1 + 1);
            switch (key)
            {
                case Keys.Tab: return "Tab";
                case Keys.Insert: return "Insert";
                case Keys.Delete: return "Delete";
                case Keys.Home: return "Home";
                case Keys.End: return "End";
                case Keys.Pause: return "Pause";
                case Keys.Scroll: return "ScrollLock";
                case Keys.Back: return "Backspace";
                case Keys.ShiftKey: case Keys.ControlKey: case Keys.Menu: case Keys.LWin: case Keys.RWin: return null;
            }
            if (key >= Keys.A && key <= Keys.Z) return ((char)key).ToString();
            if (key >= Keys.D0 && key <= Keys.D9) return ((char)('0' + (key - Keys.D0))).ToString();
            return ((int)key).ToString();
        }
    }

    sealed class FeedbackPage : WizardPage
    {
        readonly TrackBar volume = Slider(0, 100), flash = Slider(0, 100), brightness = Slider(10, 200);
        readonly Label volumeText = Ui.Text(""), flashText = Ui.Text(""), brightnessText = Ui.Text("");

        public FeedbackPage(SetupContext context) : base(context, "Feedback",
            "How loud the clicks are, how bright the button flash is, and the overall wheel brightness.")
        {
            var grid = Ui.Grid(3);
            Ui.AddRow(grid, Ui.Text("Click volume:"), volume, volumeText);
            Ui.AddRow(grid, Ui.Text("Button flash:"), flash, flashText);
            Ui.AddRow(grid, Ui.Text("Wheel brightness:"), brightness, brightnessText);
            Ui.Stack(this, Ui.Paragraph("0% click volume mutes the clicks; 0% flash turns the button highlight off."), grid);
            foreach (var slider in new[] { volume, flash, brightness }) slider.ValueChanged += (s, e) => { ShowValues(); Edited(); };
        }

        static TrackBar Slider(int min, int max) => new TrackBar { Minimum = min, Maximum = max, TickFrequency = 10, Width = 300 };

        public override string PreviewHint => "Press a push button to see the flash and hear the click with these settings.";

        protected override void Entering()
        {
            volume.Value = Percent(Settings.Real("ClickVolume", 0.5f), volume);
            flash.Value = Percent(Settings.Real("FlashStrength", 0.75f), flash);
            brightness.Value = Percent(Settings.Real("Brightness", 0.8f), brightness);
            ShowValues();
        }
        public override void Store()
        {
            Settings.Set("ClickVolume", volume.Value / 100f, "0.##");
            Settings.Set("FlashStrength", flash.Value / 100f, "0.##");
            Settings.Set("Brightness", brightness.Value / 100f, "0.##");
        }

        static int Percent(float value, TrackBar bar) => Math.Max(bar.Minimum, Math.Min(bar.Maximum, (int)Math.Round(value * 100)));
        void ShowValues() { volumeText.Text = volume.Value + "%"; flashText.Text = flash.Value + "%"; brightnessText.Text = brightness.Value + "%"; }
    }

    sealed class FinishPage : WizardPage
    {
        readonly TextBox summary = new TextBox { Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, Height = 300, Font = new System.Drawing.Font("Consolas", 9) };

        public FinishPage(SetupContext context) : base(context, "Ready to save",
            "Press Finish to write these changes to NeoXR.ini. Start the game afterwards to use them.")
        {
            Ui.Stack(this, Ui.Paragraph("Changes:"), summary,
                Ui.Paragraph("In VR: F8 recenters the wheel, F9 hides it, and the edit key lets you place it with the mouse."));
        }

        public override string PreviewHint => "This is how the wheel will look and move in VR.";

        protected override void Entering()
        {
            var changes = Settings.Changes.OrderBy(c => c.Key).Select(c => $"{c.Key} = {c.Value}").ToList();
            summary.Text = changes.Count > 0 ? string.Join(Environment.NewLine, changes) : "No changes.";
        }
    }
}
