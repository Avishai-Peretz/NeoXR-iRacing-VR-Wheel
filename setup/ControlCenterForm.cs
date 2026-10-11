using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Windows.Forms;

namespace NeoXR.Setup
{
    /// <summary>Persistent settings workspace, sharing the wizard's input and preview pipeline.</summary>
    sealed class ControlCenterForm : Form
    {
        readonly SetupContext context;
        readonly List<WizardPage> pages;
        readonly List<Button> navigation = new List<Button>();
        readonly Panel content = new Panel { Dock = DockStyle.Fill, Padding = new Padding(24), BackColor = Color.White };
        readonly Label title = new Label { Dock = DockStyle.Top, Height = 38, Font = new Font("Segoe UI", 19, FontStyle.Bold) };
        readonly Label description = new Label { Dock = DockStyle.Fill };
        readonly Label status = new Label { Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft };
        readonly Label inputStatus = new Label { Dock = DockStyle.Top, Height = 42, Padding = new Padding(16, 8, 8, 0), ForeColor = Color.Gainsboro };
        readonly Label hint = new Label { Dock = DockStyle.Bottom, Height = 92, Padding = new Padding(16), ForeColor = Color.Gainsboro };
        readonly Button save, discard;
        readonly WheelPreview preview;
        readonly Timer poll = new Timer { Interval = 33 };
        int current = -1;
        int version = -1;

        public ControlCenterForm(Settings settings)
        {
            context = new SetupContext(settings, new Devices());
            Text = "NeoXR";
            Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            ShowInTaskbar = true; MinimizeBox = true;
            Font = new Font("Segoe UI", 10);
            BackColor = Color.FromArgb(243, 245, 249);
            AutoScaleMode = AutoScaleMode.Dpi;
            ClientSize = new Size(1440, 840);
            MinimumSize = new Size(1150, 680);
            StartPosition = FormStartPosition.CenterScreen;
            pages = new List<WizardPage> {
                new OptionsPage(context, false), new DevicesPage(context), new SteeringPage(context),
                BindingPage.Buttons(context), BindingPage.Knobs(context), BindingPage.Sticks(context),
                new ClutchPage(context), new OptionsPage(context, true), new RecenterPage(context),
                new FeedbackPage(context), new DiagnosticsPage(context)
            };
            var sidebar = new FlowLayoutPanel { Dock = DockStyle.Left, Width = 215, Padding = new Padding(14, 24, 14, 10),
                FlowDirection = FlowDirection.TopDown, WrapContents = false, AutoScroll = true, BackColor = Color.FromArgb(19, 26, 39) };
            sidebar.Controls.Add(new Label { Text = "NEO XR", ForeColor = Color.White, Font = new Font("Segoe UI", 24, FontStyle.Bold), Width = 180, Height = 54 });
            sidebar.Controls.Add(new Label { Text = "CONTROL CENTER", ForeColor = Color.FromArgb(134, 153, 180), Width = 180, Height = 40 });
            string[] names = { "Overview", "Devices", "Steering", "Buttons & paddles", "Knobs & rollers", "Joysticks", "Clutch calibration", "Wheel & placement", "Shortcuts", "Sound & lighting", "Diagnostics" };
            for (int i = 0; i < pages.Count; i++) {
                int index = i;
                var button = new Button { Text = names[i], Width = 180, Height = 40, FlatStyle = FlatStyle.Flat,
                    TextAlign = ContentAlignment.MiddleLeft, ForeColor = Color.FromArgb(192, 205, 223), Margin = new Padding(0, 2, 0, 2), Cursor = Cursors.Hand };
                button.FlatAppearance.BorderSize = 0;
                button.Click += (s, e) => Navigate(index);
                navigation.Add(button); sidebar.Controls.Add(button);
            }
            var header = new Panel { Dock = DockStyle.Top, Height = 118, Padding = new Padding(24, 20, 16, 12) };
            header.Controls.Add(description); header.Controls.Add(title);
            var footer = new Panel { Dock = DockStyle.Bottom, Height = 76, Padding = new Padding(24, 16, 20, 16) };
            var actions = new FlowLayoutPanel { Dock = DockStyle.Right, Width = 300, FlowDirection = FlowDirection.RightToLeft };
            save = Ui.Button("Apply live", (s, e) => Save());
            save.BackColor = Color.FromArgb(0, 112, 190); save.ForeColor = Color.White; save.FlatStyle = FlatStyle.Flat;
            discard = Ui.Button("Discard", (s, e) => Discard());
            actions.Controls.Add(save); actions.Controls.Add(discard);
            footer.Controls.Add(status); footer.Controls.Add(actions);
            preview = new WheelPreview(settings) { Dock = DockStyle.Fill };
            var side = new Panel { Dock = DockStyle.Right, Width = 370, BackColor = WheelPreview.Backdrop };
            side.Controls.Add(preview); side.Controls.Add(inputStatus); side.Controls.Add(hint);
            Controls.Add(content); Controls.Add(side); Controls.Add(header); Controls.Add(footer); Controls.Add(sidebar);
            context.Devices.Use(settings.Integer("SteeringDevice", -1), settings.Integer("ButtonDevice", -1));
            Navigate(0);
            poll.Tick += (s, e) => {
                if (settings.ReloadIfChanged()) {
                    pages[current].Open();
                    context.Devices.Use(settings.Integer("SteeringDevice", -1), settings.Integer("ButtonDevice", -1));
                    RefreshStatus();
                }
                var input = context.Devices.Poll();
                pages[current].OnInput(input);
                if (WindowState != FormWindowState.Minimized) preview.Render(input, pages[current].Highlight);
                inputStatus.Text = input.Steering == null ? "LIVE PREVIEW  /  No steering input" : "LIVE PREVIEW  /  Steering connected";
                hint.Text = pages[current].PreviewHint + "\nDrag to orbit · Double-click to reset";
                if (version != settings.Version) RefreshStatus();
            };
            poll.Start();
        }

        void Navigate(int index)
        {
            if (index == current) return;
            if (current >= 0) pages[current].Leaving();
            current = index;
            content.Controls.Clear();
            pages[current].Open(); content.Controls.Add(pages[current]);
            title.Text = navigation[index].Text; description.Text = pages[index].Description;
            for (int i = 0; i < navigation.Count; i++) navigation[i].BackColor = i == index ? Color.FromArgb(37, 64, 94) : Color.FromArgb(19, 26, 39);
            RefreshStatus();
        }

        void RefreshStatus()
        {
            version = context.Settings.Version;
            int count = context.Settings.Changes.Count;
            save.Enabled = discard.Enabled = count > 0;
            status.Text = count > 0 ? $"{count} pending setting(s) · Apply live to save and update VR." : "Live reload active · Models and saved settings update automatically.";
            Text = "NeoXR" + (count > 0 ? " *" : "");
        }

        bool Save()
        {
            pages[current].Leaving();
            try { context.Settings.Save(); RefreshStatus(); return true; }
            catch (Exception ex) { MessageBox.Show(this, "Could not save settings:\n" + ex.Message, "NeoXR", MessageBoxButtons.OK, MessageBoxIcon.Error); return false; }
        }

        void Discard()
        {
            if (MessageBox.Show(this, "Discard all unsaved changes?", "NeoXR", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return;
            pages[current].Leaving(); context.Settings.Discard(); pages[current].Open();
            context.Devices.Use(context.Settings.Integer("SteeringDevice", -1), context.Settings.Integer("ButtonDevice", -1));
            RefreshStatus();
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            poll.Stop(); // Close exits; there is no tray process or hide-on-close behavior.
            base.OnFormClosing(e);
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing) { poll.Dispose(); foreach (var page in pages) page.Dispose(); context.Devices.Dispose(); }
            base.Dispose(disposing);
        }
    }
}
