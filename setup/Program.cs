using System;
using System.IO;
using System.Windows.Forms;

namespace NeoXR.Setup
{
    static class Program
    {
        /// <summary>Usage: NeoXR-Setup.exe [path\to\NeoXR.ini]. Defaults to the INI next to the exe.</summary>
        [STAThread]
        static int Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            string ini = args.Length > 0 ? Path.GetFullPath(args[0]) : Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "NeoXR.ini");
            if (!File.Exists(ini)) return Fail("NeoXR.ini was not found:\n" + ini);
            try { InputDevice.List(); }
            catch (DllNotFoundException) { return Fail("NeoXR-InputBridge.dll is missing. Reinstall NeoXR."); }
            using (var wizard = new WizardForm(new Settings(ini))) Application.Run(wizard);
            return 0;
        }

        static int Fail(string message)
        {
            MessageBox.Show(message, "NeoXR Setup", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }
    }
}
