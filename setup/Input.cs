using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;

namespace NeoXR.Setup
{
    /// <summary>One DirectInput reading. Indices are the zero-based numbers NeoXR.ini uses.</summary>
    sealed class InputState
    {
        public static readonly string[] AxisNames = { "X", "Y", "Z", "Rx", "Ry", "Rz", "Slider 0", "Slider 1" };

        readonly NativeState state;
        internal InputState(NativeState state) { this.state = state; }

        public int Axis(int index) => state.Axes[index];
        public bool Button(int index) => index >= 0 && index < 128 && (state.Buttons[index] & 0x80) != 0;
        /// <summary>A centred hat reports 0xFFFF in the low word.</summary>
        public bool HatCentred(int index) => (state.Pov[index] & 0xFFFF) == 0xFFFF;
        public IEnumerable<int> PressedButtons => Enumerable.Range(0, 128).Where(Button);
        public IEnumerable<int> ActiveHats => Enumerable.Range(0, 4).Where(h => !HatCentred(h));
    }

    /// <summary>A game controller opened through NeoXR-InputBridge.dll, which wraps the layer's own input code.</summary>
    sealed class InputDevice : IDisposable
    {
        IntPtr handle;
        InputDevice(IntPtr handle) { this.handle = handle; }

        public static List<string> List()
        {
            var names = new List<string>();
            int count = neo_device_name(-1, null, 0);
            for (int i = 0; i < count; i++)
            {
                var name = new StringBuilder(260);
                neo_device_name(i, name, name.Capacity);
                names.Add(name.ToString());
            }
            return names;
        }

        public static InputDevice Open(int index)
        {
            var handle = index >= 0 ? neo_open(index) : IntPtr.Zero;
            return handle == IntPtr.Zero ? null : new InputDevice(handle);
        }

        public InputState Poll() => neo_poll(handle, out var state) != 0 ? new InputState(state) : null;

        public void Dispose()
        {
            if (handle == IntPtr.Zero) return;
            neo_close(handle);
            handle = IntPtr.Zero;
        }

        const string Bridge = "NeoXR-InputBridge.dll";
        [DllImport(Bridge, CharSet = CharSet.Unicode)] static extern int neo_device_name(int index, StringBuilder name, int size);
        [DllImport(Bridge)] static extern IntPtr neo_open(int index);
        [DllImport(Bridge)] static extern int neo_poll(IntPtr device, out NativeState state);
        [DllImport(Bridge)] static extern void neo_close(IntPtr device);
    }

    /// <summary>Mirror of NeoInputState in input_bridge.cpp.</summary>
    [StructLayout(LayoutKind.Sequential)]
    struct NativeState
    {
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)] public int[] Axes;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)] public uint[] Pov;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 128)] public byte[] Buttons;
    }

    /// <summary>
    /// The steering device and the device the layer reads buttons, clutch axes and hats from.
    /// ButtonDevice=-1 means the steering device provides those too, exactly as in the layer.
    /// </summary>
    sealed class Devices : IDisposable
    {
        InputDevice steering, buttons;
        public int SteeringIndex { get; private set; } = -1;
        public int ButtonIndex { get; private set; } = -1;

        public void Use(int steeringIndex, int buttonIndex)
        {
            if (steeringIndex != SteeringIndex) { steering?.Dispose(); steering = InputDevice.Open(steeringIndex); SteeringIndex = steeringIndex; }
            if (buttonIndex != ButtonIndex) { buttons?.Dispose(); buttons = InputDevice.Open(buttonIndex); ButtonIndex = buttonIndex; }
        }

        public Snapshot Poll()
        {
            var wheel = steering?.Poll();
            return new Snapshot(wheel, ButtonIndex >= 0 ? buttons?.Poll() : wheel);
        }

        public void Dispose() { steering?.Dispose(); buttons?.Dispose(); }
    }

    /// <summary>What the layer would see this instant. Either reading is null when its device is unavailable.</summary>
    sealed class Snapshot
    {
        public Snapshot(InputState steering, InputState buttons) { Steering = steering; Buttons = buttons; }
        public InputState Steering { get; }
        public InputState Buttons { get; }
    }
}
