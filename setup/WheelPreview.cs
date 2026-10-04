using System;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace NeoXR.Setup
{
    /// <summary>
    /// The layer's own wheel renderer and animations, drawn live into this control by NeoXR-InputBridge.dll.
    /// It reads a draft copy of the settings, so every edit shows on the wheel before anything is saved.
    /// </summary>
    sealed class WheelPreview : Control
    {
        public static readonly Color Backdrop = Color.FromArgb(30, 33, 39);
        const float DefaultYaw = 0, DefaultPitch = 0.2f, MaxPitch = 1.3f, RadiansPerPixel = 0.01f;

        readonly Settings settings;
        readonly string draft = Path.Combine(Path.GetTempPath(), "NeoXR-Setup", "NeoXR.ini");
        IntPtr preview;
        int configuredVersion = -1;
        string error;
        float yaw = DefaultYaw, pitch = DefaultPitch;
        Point? dragFrom;

        public WheelPreview(Settings settings)
        {
            this.settings = settings;
            SetStyle(ControlStyles.Opaque | ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint, true);
            BackColor = Backdrop; ForeColor = Color.Gainsboro;
            Cursor = Cursors.SizeAll;
        }

        /// <summary>Animates one frame. <paramref name="highlight"/> is the control slot to point out, or -1.</summary>
        public void Render(Snapshot input, int highlight)
        {
            if (preview == IntPtr.Zero) return;
            if (configuredVersion != settings.Version)
            {
                try { settings.WriteDraft(draft); }
                catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException) { error = ex.Message; Release(); Invalidate(); return; }
                if (neo_preview_configure(preview, draft) == 0) { Fail(); return; }
                configuredVersion = settings.Version;
            }
            var steering = input.Steering == null ? null : new[] { input.Steering.Native };
            var buttons = input.Buttons == null ? null : new[] { input.Buttons.Native };
            if (neo_preview_frame(preview, steering, buttons, highlight, yaw, pitch) == 0) Fail();
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            try
            {
                settings.WriteDraft(draft);
                configuredVersion = settings.Version;
                preview = neo_preview_create(Handle, Path.GetDirectoryName(Path.GetFullPath(settings.Path)), draft, ColorTranslator.ToWin32(Backdrop));
                if (preview == IntPtr.Zero) Fail();
            }
            catch (Exception ex) when (ex is DllNotFoundException || ex is EntryPointNotFoundException || ex is IOException || ex is UnauthorizedAccessException)
            {
                error = ex.Message;
            }
        }

        protected override void OnHandleDestroyed(EventArgs e)
        {
            Release();
            try { File.Delete(draft); } catch (IOException) { } catch (UnauthorizedAccessException) { }
            base.OnHandleDestroyed(e);
        }

        protected override void OnResize(EventArgs e)
        {
            base.OnResize(e);
            if (preview != IntPtr.Zero && neo_preview_resize(preview, ClientSize.Width, ClientSize.Height) == 0) Fail();
            Invalidate();
        }

        // Only paints when there is no 3D view; otherwise the swapchain owns every pixel.
        protected override void OnPaint(PaintEventArgs e)
        {
            if (preview != IntPtr.Zero) return;
            e.Graphics.Clear(Backdrop);
            var message = "The 3D preview is not available." + (error != null ? "\n\n" + error : "");
            TextRenderer.DrawText(e.Graphics, message, Font, ClientRectangle, ForeColor,
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.WordBreak);
        }

        protected override void OnMouseDown(MouseEventArgs e) { base.OnMouseDown(e); if (e.Button == MouseButtons.Left) dragFrom = e.Location; }
        protected override void OnMouseUp(MouseEventArgs e) { base.OnMouseUp(e); dragFrom = null; }
        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e);
            if (dragFrom is Point from)
            {
                yaw -= (e.X - from.X) * RadiansPerPixel;
                pitch = Math.Max(-MaxPitch, Math.Min(MaxPitch, pitch + (e.Y - from.Y) * RadiansPerPixel));
                dragFrom = e.Location;
            }
        }
        protected override void OnDoubleClick(EventArgs e) { base.OnDoubleClick(e); yaw = DefaultYaw; pitch = DefaultPitch; }

        void Fail()
        {
            error = Marshal.PtrToStringUni(neo_preview_error());
            Release();
            Invalidate();
        }

        void Release()
        {
            if (preview == IntPtr.Zero) return;
            neo_preview_destroy(preview);
            preview = IntPtr.Zero;
        }

        const string Bridge = "NeoXR-InputBridge.dll";
        [DllImport(Bridge, CharSet = CharSet.Unicode)] static extern IntPtr neo_preview_create(IntPtr window, string folder, string settings, int background);
        [DllImport(Bridge, CharSet = CharSet.Unicode)] static extern int neo_preview_configure(IntPtr preview, string settings);
        [DllImport(Bridge)] static extern int neo_preview_resize(IntPtr preview, int width, int height);
        // A null array is passed as a null pointer, meaning "device not available".
        [DllImport(Bridge)] static extern int neo_preview_frame(IntPtr preview, [In] NativeState[] steering, [In] NativeState[] buttons, int highlight, float yaw, float pitch);
        [DllImport(Bridge)] static extern IntPtr neo_preview_error();
        [DllImport(Bridge)] static extern void neo_preview_destroy(IntPtr preview);
    }
}
