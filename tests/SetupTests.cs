using System;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Windows.Forms;

namespace NeoXR.Setup
{
    static class SetupTests
    {
        static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
        [STAThread]
        static int Main(string[] args)
        {
            Application.EnableVisualStyles();
            string folder = Path.Combine(Path.GetTempPath(), "NeoXR-tests-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(folder);
            try {
                string path = Path.Combine(folder, "NeoXR.ini");
                File.Copy(args[0], path);
                File.AppendAllText(path, "\n[Cars]\nTestCar=preserve-this\n[Other]\nCustom=keep\n");
                var settings = new Settings(path);
                string original = File.ReadAllText(path);
                settings.Set("Brightness", .65f, "0.##");
                Check(File.ReadAllText(path) == original, "Edits must stay staged");
                settings.Discard();
                Check(settings.Changes.Count == 0 && File.ReadAllText(path) == original, "Discard changed disk");
                CultureInfo.CurrentCulture = new CultureInfo("de-DE");
                settings.Set("Brightness", .65f, "0.##");
                settings.WriteDraft(Path.Combine(folder, "draft.ini"));
                Check(File.ReadAllText(path) == original, "Preview draft changed disk");
                settings.Save();
                Check(new Settings(path).Text("Brightness") == "0.65", "Numbers must use invariant culture");
                Check(File.ReadAllText(path).Contains("TestCar=preserve-this") && File.ReadAllText(path).Contains("Custom=keep"), "Save lost unrelated sections");
                settings.ReloadIfChanged(); settings.Set("Brightness", .9f, "0.##");
                var external = new Settings(path); external.Set("WidthMm", 320); external.Save();
                Check(settings.ReloadIfChanged(), "External settings change not detected");
                Check(settings.Text("Brightness") == "0.9" && settings.Integer("WidthMm", 0) == 320, "External reload lost pending edits or saved values");
                settings.Discard();
                settings.Set("RotationDegrees", 600); settings.Set("SteeringAxis", 0); settings.Set("Invert", false);
                settings.Set("SteeringZeroOffsetDegrees", 0);
                using (var devices = new Devices())
                using (var steeringPage = new SteeringPage(new SetupContext(settings, devices))) {
                    steeringPage.Open();
                    var offset = Descendants(steeringPage).OfType<NumericUpDown>().Single(c => c.Name == "SteeringZeroOffset");
                    var zero = Descendants(steeringPage).OfType<Button>().Single(c => c.Text == "Set current as zero");
                    Check(!zero.Enabled, "Zero capture must require steering input");
                    var axes = new int[8]; axes[0] = 100;
                    steeringPage.OnInput(new Snapshot(new InputState(new NativeState { Axes = axes }), null));
                    typeof(Button).GetMethod("OnClick", BindingFlags.NonPublic | BindingFlags.Instance).Invoke(zero, new object[] { EventArgs.Empty });
                    Check(offset.Value == 3 && settings.Real("SteeringZeroOffsetDegrees", 0) == 3, "Capture did not subtract the current three-degree reading");
                    offset.Value = -2.5m;
                    Check(settings.Real("SteeringZeroOffsetDegrees", 0) == -2.5f, "Manual zero offset was not staged");
                    steeringPage.OnInput(new Snapshot(null, null)); Check(!zero.Enabled, "Disconnected steering left zero capture enabled");
                    settings.Save(); settings.Discard(); steeringPage.Open();
                    Check(offset.Value == -2.5m, "Saved zero offset did not reload");
                }
                settings.Set("SteeringDevice", 3); settings.Save();
                using (var center = new ControlCenterForm(settings)) {
                    Check(center.ShowInTaskbar && center.MinimizeBox, "Window must remain a normal taskbar app");
                    var navigate = typeof(ControlCenterForm).GetMethod("Navigate", BindingFlags.NonPublic | BindingFlags.Instance);
                    for (int i = 0; i < 11; i++) { navigate.Invoke(center, new object[] { i }); center.PerformLayout(); }
                    Check(settings.Integer("SteeringDevice", -1) == 3, "Unplugged device selection was lost");
                    navigate.Invoke(center, new object[] { 8 });
                    var keys = Descendants(center).OfType<KeyBox>().ToList();
                    keys[0].KeyName = "F12";
                    Check(settings.Text("RecenterKey") == "F12", "Shortcut edits were not staged immediately");
                    navigate.Invoke(center, new object[] { 0 });
                }
                if (args.Contains("--lifecycle")) {
                    settings.Discard(); settings.Set("Model", "placeholder"); settings.Set("ClickVolume", 0); settings.Save();
                    Exception lifecycleError = null;
                    using (var window = new ControlCenterForm(settings))
                    using (var timer = new System.Windows.Forms.Timer { Interval = 250 }) {
                        window.StartPosition = FormStartPosition.Manual; window.Location = new Point(-32000, -32000);
                        int step = 0;
                        timer.Tick += (s, e) => {
                            try {
                                if (step++ == 0) window.WindowState = FormWindowState.Minimized;
                                else {
                                    Check(!window.IsDisposed && window.WindowState == FormWindowState.Minimized, "Minimize stopped the app");
                                    timer.Stop(); window.Close();
                                }
                            } catch (Exception ex) { lifecycleError = ex; timer.Stop(); window.Close(); }
                        };
                        timer.Start(); Application.Run(window);
                        Check(window.IsDisposed, "Close did not dispose the window and end its event loop");
                    }
                    if (lifecycleError != null) throw lifecycleError;
                    Console.WriteLine("PASS: native window minimize keeps event loop alive; close exits and disposes resources.");
                }
                Console.WriteLine("PASS: staged edits, atomic save, discard, preview isolation, invariant numbers, section preservation, external reload with pending edits, all 11 pages, offline device retention, shortcut staging, taskbar window properties.");
                return 0;
            } catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
            finally { Directory.Delete(folder, true); }
        }
        static IEnumerable<Control> Descendants(Control parent) {
            foreach (Control child in parent.Controls) { yield return child; foreach (var descendant in Descendants(child)) yield return descendant; }
        }
    }
}
