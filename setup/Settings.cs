using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace NeoXR.Setup
{
    /// <summary>
    /// The [Wheel] section of NeoXR.ini, read with the same Win32 calls the layer uses.
    /// Edits are kept in memory until <see cref="Save"/>, so Cancel really discards them.
    /// </summary>
    sealed class Settings
    {
        const string Section = "Wheel";
        static readonly CultureInfo Invariant = CultureInfo.InvariantCulture;
        readonly Dictionary<string, string> changes = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

        public Settings(string path) { Path = path; }

        public string Path { get; }
        public IReadOnlyDictionary<string, string> Changes => changes;
        /// <summary>Increases whenever the pending changes do, so views know when to refresh.</summary>
        public int Version { get; private set; }

        public string Text(string key, string fallback = "") =>
            changes.TryGetValue(key, out var value) ? value : Saved(key, fallback);
        public int Integer(string key, int fallback) =>
            int.TryParse(Text(key), NumberStyles.Integer, Invariant, out var value) ? value : fallback;
        public float Real(string key, float fallback) =>
            float.TryParse(Text(key), NumberStyles.Float, Invariant, out var value) ? value : fallback;
        public bool Flag(string key, bool fallback) => Integer(key, fallback ? 1 : 0) != 0;

        /// <summary>Records a change only if it differs from the file, so the summary lists real edits.</summary>
        public void Set(string key, string value) => Record(key, value, Saved(key) != value);
        // Numbers compare by value, so "1" and "1.0" or "0.80" and "0.8" are not reported as edits.
        public void Set(string key, int value) => Record(key, value.ToString(Invariant),
            !int.TryParse(Saved(key), NumberStyles.Integer, Invariant, out var old) || old != value);
        public void Set(string key, bool value) => Set(key, value ? 1 : 0);
        public void Set(string key, float value, string format)
        {
            var text = value.ToString(format, Invariant);
            Record(key, text, !float.TryParse(Saved(key), NumberStyles.Float, Invariant, out var old) ||
                              Math.Abs(old - float.Parse(text, Invariant)) > 1e-6f);
        }

        public void Save()
        {
            foreach (var change in changes)
                if (!WritePrivateProfileString(Section, change.Key, change.Value, Path)) throw new Win32Exception();
            changes.Clear();
            Version++;
        }

        /// <summary>Writes the file as it would be after Save to <paramref name="path"/>, leaving NeoXR.ini untouched.</summary>
        public void WriteDraft(string path)
        {
            Directory.CreateDirectory(System.IO.Path.GetDirectoryName(path));
            File.WriteAllBytes(path, File.Exists(Path) ? File.ReadAllBytes(Path) : new byte[0]);
            foreach (var change in changes)
                if (!WritePrivateProfileString(Section, change.Key, change.Value, path)) throw new Win32Exception();
        }

        string Saved(string key, string fallback = "")
        {
            var buffer = new StringBuilder(512);
            GetPrivateProfileString(Section, key, fallback, buffer, buffer.Capacity, Path);
            return buffer.ToString();
        }

        void Record(string key, string value, bool differs)
        {
            bool had = changes.TryGetValue(key, out var before);
            if (differs) changes[key] = value; else changes.Remove(key);
            if (had != differs || (differs && before != value)) Version++;
        }

        [DllImport("kernel32", CharSet = CharSet.Unicode)]
        static extern int GetPrivateProfileString(string section, string key, string fallback, StringBuilder value, int size, string path);
        [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern bool WritePrivateProfileString(string section, string key, string value, string path);
    }
}
