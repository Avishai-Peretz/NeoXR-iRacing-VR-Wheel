using System;
using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;

namespace NeoXR.Setup
{
    /// <summary>The wizard frame: header, current page, Back/Next/Cancel, and a timer that feeds live input to the page.</summary>
    sealed class WizardForm : Form
    {
        readonly SetupContext context;
        readonly List<WizardPage> pages;
        readonly Label title = new Label { AutoSize = true, Font = new Font("Segoe UI", 13f, FontStyle.Bold), Dock = DockStyle.Top };
        readonly Label description = new Label { AutoSize = true, Dock = DockStyle.Top, Padding = new Padding(0, 4, 0, 0) };
        readonly Panel content = new Panel { Dock = DockStyle.Fill, Padding = new Padding(20, 16, 20, 8) };
        readonly Button back, next, cancel;
        readonly Timer poll = new Timer { Interval = 30 };
        int current = -1;
        bool saved;

        public WizardForm(Settings settings)
        {
            context = new SetupContext(settings, new Devices());
            Text = "NeoXR Setup";
            Font = new Font("Segoe UI", 9f);
            AutoScaleMode = AutoScaleMode.Dpi;
            ClientSize = new Size(780, 600);
            MinimumSize = new Size(700, 520);
            StartPosition = FormStartPosition.CenterScreen;

            var header = new Panel { Dock = DockStyle.Top, Height = 78, BackColor = Color.White, Padding = new Padding(20, 14, 20, 8) };
            header.Controls.Add(description); header.Controls.Add(title);
            var footer = new FlowLayoutPanel { Dock = DockStyle.Bottom, FlowDirection = FlowDirection.RightToLeft, AutoSize = true, Padding = new Padding(12) };
            cancel = Ui.Button("Cancel", (s, e) => Close());
            next = Ui.Button("Next >", (s, e) => Next());
            back = Ui.Button("< Back", (s, e) => Show(current - 1));
            footer.Controls.AddRange(new Control[] { cancel, next, back });
            Controls.Add(content); Controls.Add(footer); Controls.Add(header);
            AcceptButton = next;

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

            context.Devices.Use(settings.Integer("SteeringDevice", -1), settings.Integer("ButtonDevice", -1));
            poll.Tick += (s, e) => pages[current].OnInput(context.Devices.Poll());
            poll.Start();
            Show(0);
        }

        void Show(int index)
        {
            if (current >= 0) pages[current].Leaving();
            current = index;
            var page = pages[index];
            content.Controls.Clear();
            page.Entering();
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
        }

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
            if (disposing) { poll.Dispose(); context.Devices.Dispose(); }
            base.Dispose(disposing);
        }
    }
}
