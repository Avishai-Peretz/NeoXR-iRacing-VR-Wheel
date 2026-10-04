using System;
using System.Linq;
using System.Windows.Forms;

namespace NeoXR.Setup
{
    sealed class WelcomePage : WizardPage
    {
        readonly CheckBox enabled = new CheckBox { Text = "Show the NeoXR wheel in VR", AutoSize = true };
        readonly TextBox process = new TextBox { Width = 260 };

        public WelcomePage(SetupContext context) : base(context, "Welcome to NeoXR setup",
            "This wizard maps your wheel's controls so the virtual wheel moves exactly like the real one.")
        {
            var grid = Ui.Grid(2);
            Ui.AddRow(grid, enabled);
            Ui.AddRow(grid, Ui.Text("Game executable:"), process);
            Ui.Stack(this,
                Ui.Paragraph("Close the game before you start. Connect the wheelbase and the wheel. " +
                             "Every page shows live input, so you can check each control as you go. " +
                             "Nothing is saved until you press Finish."),
                grid,
                Ui.Paragraph("Settings file: " + context.Settings.Path));
        }

        public override void Entering()
        {
            enabled.Checked = Settings.Flag("Enabled", false);
            process.Text = Settings.Text("Process", "iRacingSim64DX11.exe");
        }
        public override void Leaving()
        {
            Settings.Set("Enabled", enabled.Checked);
            Settings.Set("Process", process.Text.Trim());
        }
    }

    sealed class DevicesPage : WizardPage
    {
        const string SameDevice = "Same as the steering device";
        readonly ComboBox steering = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 380 };
        readonly ComboBox buttons = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 380 };
        readonly Label steeringActivity = Ui.Text(""), buttonActivity = Ui.Text("");

        public DevicesPage(SetupContext context) : base(context, "Devices",
            "Pick the wheelbase that reports steering, and the device that reports the wheel's buttons.")
        {
            var grid = Ui.Grid(2);
            Ui.AddRow(grid, Ui.Text("Steering:"), steering);
            Ui.AddRow(grid, null, steeringActivity);
            Ui.AddRow(grid, Ui.Text("Buttons:"), buttons);
            Ui.AddRow(grid, null, buttonActivity);
            Ui.AddRow(grid, null, Ui.Button("Refresh device list", (s, e) => Fill(steering.SelectedIndex, buttons.SelectedIndex - 1)));
            Ui.Stack(this,
                Ui.Paragraph("Most Simagic setups report the NEO X buttons through the wheelbase, so keep " +
                             "\"" + SameDevice + "\" unless the buttons only show up on another device. " +
                             "Turn the wheel and press a button: the activity lines below show what each device sees."),
                grid);
            steering.SelectedIndexChanged += (s, e) => { Apply(); NotifyStateChanged(); };
            buttons.SelectedIndexChanged += (s, e) => Apply();
        }

        public override bool CanContinue => steering.SelectedIndex >= 0;

        public override void Entering() => Fill(Settings.Integer("SteeringDevice", -1), Settings.Integer("ButtonDevice", -1));
        public override void Leaving()
        {
            Settings.Set("SteeringDevice", steering.SelectedIndex);
            Settings.Set("ButtonDevice", buttons.SelectedIndex - 1);
        }

        public override void OnInput(Snapshot input)
        {
            steeringActivity.Text = Describe(input.Steering);
            buttonActivity.Text = Describe(input.Buttons);
        }

        void Fill(int steeringIndex, int buttonIndex)
        {
            var names = InputDevice.List().Select((name, i) => $"{i}: {name}").ToArray();
            steering.Items.Clear(); steering.Items.AddRange(names);
            buttons.Items.Clear(); buttons.Items.Add(SameDevice); buttons.Items.AddRange(names);
            steering.SelectedIndex = steeringIndex < names.Length ? steeringIndex : -1;
            buttons.SelectedIndex = buttonIndex + 1 < buttons.Items.Count ? buttonIndex + 1 : 0;
            Apply();
        }

        void Apply() => Context.Devices.Use(steering.SelectedIndex, buttons.SelectedIndex - 1);

        static string Describe(InputState state)
        {
            if (state == null) return "No input";
            var pressed = string.Join(", ", state.PressedButtons);
            var hats = string.Join(", ", state.ActiveHats.Select(h => "hat " + h));
            return $"Axis X {state.Axis(0),6}   Buttons: {(pressed.Length > 0 ? pressed : "none")}{(hats.Length > 0 ? "   " + hats : "")}";
        }
    }

    sealed class SteeringPage : WizardPage
    {
        const int DetectThreshold = 4000;
        readonly ComboBox axis = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 120 };
        readonly CheckBox invert = new CheckBox { Text = "Invert", AutoSize = true };
        readonly NumericUpDown rotation = new NumericUpDown { Minimum = 90, Maximum = 2520, Increment = 10, Width = 90 };
        readonly ProgressBar position = new ProgressBar { Minimum = 0, Maximum = 20000, Width = 380 };
        readonly Label angle = Ui.Text(""), status = Ui.Text("");
        readonly Button detect;
        int[] baseline;

        public SteeringPage(SetupContext context) : base(context, "Steering",
            "Tell NeoXR which axis is the steering and how far the wheel turns lock to lock.")
        {
            axis.Items.AddRange(InputState.AxisNames);
            detect = Ui.Button("Detect", (s, e) => StartDetect());
            var grid = Ui.Grid(3);
            var axisRow = new FlowLayoutPanel { AutoSize = true, Margin = new Padding(0), WrapContents = false };
            axisRow.Controls.AddRange(new Control[] { axis, detect });
            Ui.AddRow(grid, Ui.Text("Axis:"), axisRow);
            Ui.AddRow(grid, null, invert);
            Ui.AddRow(grid, Ui.Text("Rotation (degrees):"), rotation);
            Ui.AddRow(grid, Ui.Text("Live:"), position, angle);
            Ui.AddRow(grid, null, status);
            Ui.Stack(this,
                Ui.Paragraph("Centre the wheel, press Detect, then turn the wheel clearly to the RIGHT. " +
                             "Rotation must match the full lock-to-lock angle set in SimPro (e.g. 900)."),
                grid);
        }

        public override void Entering()
        {
            axis.SelectedIndex = Math.Max(0, Math.Min(7, Settings.Integer("SteeringAxis", 0)));
            invert.Checked = Settings.Flag("Invert", false);
            rotation.Value = (decimal)Math.Max(90, Math.Min(2520, Settings.Real("RotationDegrees", 900)));
        }
        public override void Leaving()
        {
            detecting = false; detect.Enabled = true;
            Settings.Set("SteeringAxis", axis.SelectedIndex);
            Settings.Set("Invert", invert.Checked);
            Settings.Set("RotationDegrees", (float)rotation.Value, "0");
        }

        public override void OnInput(Snapshot input)
        {
            var state = input.Steering;
            if (state == null) { status.Text = "The steering device is not responding."; return; }
            if (detecting && baseline == null) baseline = Enumerable.Range(0, 8).Select(state.Axis).ToArray();
            else if (detecting) Detect(state);
            int raw = state.Axis(axis.SelectedIndex) * (invert.Checked ? -1 : 1);
            position.Value = Math.Max(0, Math.Min(20000, raw + 10000));
            angle.Text = $"{raw / 10000f * (float)rotation.Value / 2:0}°";
        }

        bool detecting;
        void StartDetect() { detecting = true; baseline = null; status.Text = "Turn the wheel to the right..."; detect.Enabled = false; }

        // The axis that moved furthest from where it was when Detect was pressed is the steering.
        void Detect(InputState state)
        {
            int best = -1, bestDelta = 0;
            for (int a = 0; a < 8; a++)
            {
                int delta = state.Axis(a) - baseline[a];
                if (Math.Abs(delta) > Math.Abs(bestDelta)) { best = a; bestDelta = delta; }
            }
            if (Math.Abs(bestDelta) < DetectThreshold) return;
            axis.SelectedIndex = best;
            invert.Checked = bestDelta < 0;  // turning right must read positive
            status.Text = $"Detected axis {InputState.AxisNames[best]}{(invert.Checked ? ", inverted" : "")}. Turn the wheel to check the live bar.";
            detecting = false; detect.Enabled = true;
        }
    }
}
