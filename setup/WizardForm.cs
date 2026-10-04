using System;
using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;

namespace NeoXR.Setup
{
    /// <summary>
    /// The wizard frame: step list, header, current page, live wheel preview, Back/Next/Cancel,
    /// and a timer that feeds live input to the page and the preview.
    /// </summary>
    sealed class WizardForm : Form
    {
        static readonly Color Accent = Color.FromArgb(0, 120, 215), StepsBack = Color.FromArgb(243, 244, 247);
        readonly SetupContext context;
        readonly List<WizardPage> pages;
        readonly List<Label> steps = new List<Label>();
        readonly Label title = new Label { AutoSize = true, Font = new Font("Segoe UI", 13f, FontStyle.Bold), Dock = DockStyle.Top };
        readonly Label description = new Label { AutoSize = true, Dock = DockStyle.Top, Padding = new Padding(0, 4, 0, 0) };
        readonly Panel content = new Panel { Dock = DockStyle.Fill, Padding = new Padding(20, 16, 20, 8) };
        readonly WheelPreview preview;
        readonly Label hint = new Label
        {
            Dock = DockStyle.Bottom, Height = 52, Padding = new Padding(12, 6, 12, 6), TextAlign = ContentAlignment.MiddleLeft,
            BackColor = WheelPreview.Backdrop, ForeColor = Color.Gainsboro
        };
        readonly Button back, next, cancel;
        readonly Font currentStepFont;
        readonly Timer poll = new Timer { Interval = 30 };
        int current = -1, furthest;
        bool saved;

        public WizardForm(Settings settings)
        {
            context = new SetupContext(settings, new Devices());
            Text = "NeoXR Setup";
            Font = new Font("Segoe UI", 9f);
            currentStepFont = new Font(Font, FontStyle.Bold);
            AutoScaleMode = AutoScaleMode.Dpi;
            ClientSize = new Size(1320, 700);
            MinimumSize = new Size(1120, 580);
            StartPosition = FormStartPosition.CenterScreen;

            pages = new List<WizardPage>
            {
                new WelcomePage(context),
                new DevicesPage(context),
                new SteeringPage(context),
                BindingPage.Buttons(context),
                BindingPage.Knobs(context),
                BindingPage.Sticks(context),
                new ClutchPage(context),
                new RecenterPage(context),
                new FeedbackPage(context),
                new FinishPage(context),
            };
            foreach (var page in pages) page.StateChanged += (s, e) => UpdateButtons();

            var header = new Panel { Dock = DockStyle.Top, Height = 78, BackColor = Color.White, Padding = new Padding(20, 14, 20, 8) };
            header.Controls.Add(description); header.Controls.Add(title);
            var footer = new FlowLayoutPanel { Dock = DockStyle.Bottom, FlowDirection = FlowDirection.RightToLeft, AutoSize = true, Padding = new Padding(12) };
            cancel = Ui.Button("Cancel", (s, e) => Close());
            next = Ui.Button("Next >", (s, e) => Next());
            back = Ui.Button("< Back", (s, e) => Show(current - 1));
            footer.Controls.AddRange(new Control[] { cancel, next, back });

            preview = new WheelPreview(settings) { Dock = DockStyle.Fill };
            var side = new Panel { Dock = DockStyle.Right, Width = 440, BackColor = WheelPreview.Backdrop };
            side.Controls.Add(preview); side.Controls.Add(hint);

            // Docking runs from the last control added to the first: the footer spans the full width, the step list the full height.
            Controls.Add(content); Controls.Add(side); Controls.Add(header); Controls.Add(StepList()); Controls.Add(footer);
            AcceptButton = next;

            context.Devices.Use(settings.Integer("SteeringDevice", -1), settings.Integer("ButtonDevice", -1));
            poll.Tick += (s, e) => Tick();
            poll.Start();
            Show(0);
        }

        Control StepList()
        {
            var list = new FlowLayoutPanel
            {
                Dock = DockStyle.Left, Width = 190, FlowDirection = FlowDirection.TopDown, WrapContents = false,
                BackColor = StepsBack, Padding = new Padding(10, 16, 10, 10)
            };
            for (int i = 0; i < pages.Count; i++)
            {
                int index = i;
                var step = new Label { AutoSize = true, MaximumSize = new Size(170, 0), Padding = new Padding(4, 5, 4, 5), Text = $"{i + 1}.  {pages[i].Title}" };
                step.Click += (s, e) => { if (CanJumpTo(index)) Show(index); };
                steps.Add(step);
                list.Controls.Add(step);
            }
            return list;
        }

        void Tick()
        {
            var page = pages[current];
            var input = context.Devices.Poll();
            page.OnInput(input);
            preview.Render(input, page.Highlight);
            hint.Text = page.PreviewHint;
        }

        void Show(int index)
        {
            if (current >= 0) pages[current].Leaving();
            current = index;
            furthest = Math.Max(furthest, index);
            var page = pages[index];
            content.Controls.Clear();
            page.Open();
            content.Controls.Add(page);
            title.Text = page.Title;
            description.Text = page.Description;
            UpdateButtons();
        }

        void UpdateButtons()
        {
            bool last = current == pages.Count - 1;
            back.Enabled = current > 0;
            next.Text = last ? "Finish" : "Next >";
            next.Enabled = pages[current].CanContinue;
            Text = $"NeoXR Setup - step {current + 1} of {pages.Count}";
            for (int i = 0; i < steps.Count; i++)
            {
                steps[i].Font = i == current ? currentStepFont : Font;
                steps[i].ForeColor = i == current ? Accent : i <= furthest ? SystemColors.ControlText : SystemColors.GrayText;
                steps[i].Cursor = CanJumpTo(i) ? Cursors.Hand : Cursors.Default;
            }
        }

        // Any step already visited, but forward only when the current page is complete.
        bool CanJumpTo(int index) => index != current && index <= furthest && (index < current || pages[current].CanContinue);

        void Next()
        {
            if (current < pages.Count - 1) { Show(current + 1); return; }
            pages[current].Leaving();
            try { context.Settings.Save(); }
            catch (Exception e) { MessageBox.Show(this, "Could not save NeoXR.ini:\n" + e.Message, "NeoXR Setup", MessageBoxButtons.OK, MessageBoxIcon.Error); return; }
            saved = true;
            MessageBox.Show(this, "Settings saved. Start the game to use them.", "NeoXR Setup", MessageBoxButtons.OK, MessageBoxIcon.Information);
            Close();
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            if (!saved)
            {
                pages[current].Leaving();
                if (context.Settings.Changes.Count > 0 &&
                    MessageBox.Show(this, "Discard your changes?", "NeoXR Setup", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes)
                {
                    e.Cancel = true;
                    return;
                }
            }
            base.OnFormClosing(e);
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing) { poll.Dispose(); context.Devices.Dispose(); currentStepFont.Dispose(); }
            base.Dispose(disposing);
        }
    }
}
