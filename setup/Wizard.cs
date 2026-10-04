using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Windows.Forms;

namespace NeoXR.Setup
{
    /// <summary>What every page works with.</summary>
    sealed class SetupContext
    {
        public SetupContext(Settings settings, Devices devices) { Settings = settings; Devices = devices; }
        public Settings Settings { get; }
        public Devices Devices { get; }
    }

    /// <summary>
    /// One step of the wizard. Pages load from <see cref="Settings"/> when shown and store back when left,
    /// so moving Back and Next never loses edits and nothing touches the file until Finish.
    /// </summary>
    abstract class WizardPage : UserControl
    {
        protected WizardPage(SetupContext context, string title, string description)
        {
            Context = context; Title = title; Description = description;
            Dock = DockStyle.Fill; AutoScroll = true;
        }
        protected SetupContext Context { get; }
        protected Settings Settings => Context.Settings;
        public string Title { get; }
        public string Description { get; }
        public virtual bool CanContinue => true;
        public event EventHandler StateChanged;
        protected void NotifyStateChanged() => StateChanged?.Invoke(this, EventArgs.Empty);

        public virtual void Entering() { }
        public virtual void Leaving() { }
        /// <summary>Called about 30 times a second with the current device readings.</summary>
        public virtual void OnInput(Snapshot input) { }
    }

    /// <summary>Small layout helpers so pages read as a list of rows instead of coordinates.</summary>
    static class Ui
    {
        public static TableLayoutPanel Grid(int columns)
        {
            var grid = new TableLayoutPanel { ColumnCount = columns, AutoSize = true, Dock = DockStyle.Top, Padding = new Padding(0, 0, 0, 8) };
            for (int c = 0; c < columns; c++) grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            return grid;
        }
        public static Label Text(string text, bool bold = false) => new Label
        {
            Text = text, AutoSize = true, Margin = new Padding(3, 7, 12, 3),
            Font = bold ? new Font(SystemFonts.MessageBoxFont, FontStyle.Bold) : null
        };
        public static Label Paragraph(string text) => new Label
        {
            Text = text, AutoSize = true, MaximumSize = new Size(680, 0), Dock = DockStyle.Top, Padding = new Padding(0, 0, 0, 10)
        };
        public static Button Button(string text, EventHandler click)
        {
            var button = new Button { Text = text, AutoSize = true, Margin = new Padding(3, 3, 6, 3) };
            button.Click += click;
            return button;
        }
        /// <summary>Adds controls top to bottom; Dock=Top stacks in reverse, so add them reversed.</summary>
        public static void Stack(Control parent, params Control[] children)
        {
            foreach (var child in children.Reverse()) { child.Dock = DockStyle.Top; parent.Controls.Add(child); }
        }
        public static void AddRow(TableLayoutPanel grid, params Control[] cells)
        {
            int row = grid.RowCount++;
            for (int c = 0; c < cells.Length; c++) if (cells[c] != null) grid.Controls.Add(cells[c], c, row);
        }
    }

    enum BindingKind { Button, Hat }

    /// <summary>
    /// One "press a control to bind it" row. While listening it takes the first button (or hat) that goes
    /// from released to pressed, so controls already held when you click Bind are ignored.
    /// </summary>
    sealed class BindingRow
    {
        static readonly Color Held = Color.FromArgb(200, 240, 200);
        readonly BindingKind kind;
        InputState previous;
        bool listening;

        public BindingRow(string key, string label, BindingKind kind = BindingKind.Button)
        {
            Key = key; this.kind = kind;
            Name = Ui.Text(label);
            Value = new Label { AutoSize = false, Width = 190, Height = 23, TextAlign = ContentAlignment.MiddleLeft, Margin = new Padding(3, 3, 6, 3), BorderStyle = BorderStyle.FixedSingle };
            Bind = Ui.Button("Bind", (s, e) => { if (listening) Stop(); else ListenRequested?.Invoke(this); });
            Clear = Ui.Button("Clear", (s, e) => { Stop(); Assign(-1); });
        }

        public string Key { get; }
        public BindingKind Kind => kind;
        public int Index { get; private set; } = -1;
        public Label Name { get; }
        public Label Value { get; }
        public Button Bind { get; }
        public Button Clear { get; }
        public event Action<BindingRow> ListenRequested;
        public event Action<BindingRow> Changed;

        public void Load(Settings settings) { Index = settings.Integer(Key, -1); Show(false); }
        public void Store(Settings settings) => settings.Set(Key, Index);
        public void Listen() { listening = true; previous = null; Bind.Text = "Press it now... (cancel)"; }
        public void Stop() { if (!listening) return; listening = false; Bind.Text = "Bind"; }
        public void MarkDuplicate(bool duplicate) => Value.ForeColor = duplicate ? Color.DarkOrange : SystemColors.ControlText;

        public void OnInput(InputState input)
        {
            if (input == null) return;
            if (listening && previous != null)
            {
                int pressed = kind == BindingKind.Button
                    ? input.PressedButtons.FirstOrDefault(b => !previous.Button(b), -1)
                    : input.ActiveHats.FirstOrDefault(h => previous.HatCentred(h), -1);
                if (pressed >= 0) { Stop(); Assign(pressed); }
            }
            previous = input;
            bool held = Index >= 0 && (kind == BindingKind.Button ? input.Button(Index) : !input.HatCentred(Index));
            Value.BackColor = held ? Held : SystemColors.Window;
        }

        void Assign(int index) { Index = index; Show(true); Changed?.Invoke(this); }

        // SimPro and Windows' controller panel number buttons from 1; NeoXR.ini uses DirectInput's 0-based index.
        void Show(bool justBound)
        {
            Value.Text = Index < 0 ? "Not set"
                : kind == BindingKind.Hat ? $"Hat {Index}"
                : $"Button {Index}  (SimPro {Index + 1})";
            if (justBound) Value.Text += "  ✓";
        }
    }

    /// <summary>A set of binding rows where only one listens at a time and duplicates are highlighted.</summary>
    sealed class BindingGroup
    {
        readonly List<BindingRow> rows = new List<BindingRow>();
        public IReadOnlyList<BindingRow> Rows => rows;

        public BindingRow Add(BindingRow row)
        {
            row.ListenRequested += r => { foreach (var other in rows) other.Stop(); r.Listen(); };
            row.Changed += r => MarkDuplicates();
            rows.Add(row);
            return row;
        }
        public void Load(Settings settings) { foreach (var row in rows) row.Load(settings); MarkDuplicates(); }
        public void Store(Settings settings) { foreach (var row in rows) { row.Stop(); row.Store(settings); } }
        public void OnInput(InputState input) { foreach (var row in rows) row.OnInput(input); }

        void MarkDuplicates()
        {
            foreach (var row in rows)
                row.MarkDuplicate(row.Index >= 0 && rows.Count(r => r.Index == row.Index && r.Kind == row.Kind) > 1);
        }
    }

    static class Extensions
    {
        public static int FirstOrDefault(this IEnumerable<int> source, Func<int, bool> predicate, int fallback)
        {
            foreach (var item in source) if (predicate(item)) return item;
            return fallback;
        }
    }
}
