# NeoXR 0.2.1: custom wheel model

Version 0.2.1 explicitly creates an sRGB render-target view for OpenXR typeless swapchain textures, fixing a likely E_INVALIDARG draw failure. Render-target creation and command-list errors are now labeled separately in the log. This fix requires a Windows build and headset test.

This update adds Avi's modeled wheel to the existing Windows D3D11/OpenXR overlay. The previous hardcoded wheel was reported working in iRacing on Quest 3 with the Meta runtime. Version 0.2 preserves that layer integration, steering input, placement and F8/F9 controls, and adds a textured model renderer.

**Validation:** portable steering and asset-loader checks pass against the supplied model, including malformed-file rejection. The new Windows renderer has not been compiled or tested in VR in this workspace. Build and headset validation on the actual PC are still required. No prebuilt Windows DLL is included.

## Update the working installation

Close iRacing. Extract this source archive into a **new folder**, then open PowerShell in the extracted `NeoXR` folder and run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Build-Update.ps1 -InstalledPackage "D:\Downloads\NeoXR-0.1-source\NeoXR\build\package"
```

Change the path if the working package is elsewhere. The script builds x64 Release, runs tests, installs into the new source's build/package folder, backs up the old DLL and settings, then copies the new assets and DLL into the existing package. Your INI and registered manifest are retained. No re-registration is needed. It stops before updating if compilation or tests fail.

Start iRacing in OpenXR, sit normally and press **F8**. **F9** hides/shows the wheel. **Ctrl+Shift+Down** moves the wheel 1 cm closer and **Ctrl+Shift+Up** moves it 1 cm away. **Ctrl+Shift+PageUp** tilts the top of the wheel 1° toward you and **Ctrl+Shift+PageDown** tilts it 1° away. The new values are saved as `Z` and `TiltDegrees` in NeoXR.ini. If the old INI has no Model entry, the default is now `custom`. To select explicitly, add these entries under the existing `[Wheel]` section:

```ini
Model=custom
AssetDirectory=assets
Brightness=0.8
ButtonTravelMm=1.2
PaddleAngleDegrees=7
LeftPaddleButton=-1
RightPaddleButton=-1
LeftClutchButton=-1
RightClutchButton=-1
```

Keep your working Enabled, Process, SteeringDevice, ButtonDevice, SteeringAxis, RotationDegrees, Invert, WidthMm, X/Y/Z and TiltDegrees values.

To return to the original hardcoded visual, set `Model=placeholder` and restart iRacing. This uses the original geometry and coloring through the same frame path. To restore the original DLL completely, close iRacing and copy NeoXR.dll from the timestamped backup folder into the package. If you have already customized this installation's asset files, the backup also contains those.

## Included model and controls

The runtime asset has 23 separate parts, 206,786 triangles, and baked base-color, normal and metallic/roughness texture atlases. All steering geometry rotates together. The loader measures the transformed mesh's actual X extent, then scales it to your configured WidthMm. The prepared model is upright with the front facing positive Z.

- `Button0` through `Button7` now map to the model objects named **B1_push_btn through B8_push_btn**, respectively. In placeholder mode they retain the original visual order. These are zero-based DirectInput indices, not an assumption about SIMAGIC numbering. Verify each with NeoXR-Input.exe on the hub device.
- Every control has click feedback: a springy press animation, a short highlight flash (`FlashStrength`) and a synthesized click through the Windows default audio device (`ClickVolume`, or your own WAV via `ClickSoundFile`). Knob detents tick, buttons click and paddles clack.
- The rotary knobs turn one `KnobStepDegrees` per detent. The round B37/B38 and B39/B40 knobs turn freely; the B9/B10 and B11/B12 thumb rollers nudge and settle back because only their front arc is modeled. Defaults assume B9 is DirectInput button 8, and so on; set the `...KnobCW/CCW` keys and swap them if a knob turns backwards.
- The joysticks tilt by `StickAngleDegrees` and depress on push. Map them with `LeftStickUp/Down/Left/Right/Push` (and `RightStick...`) or, if they report as a hat switch, `LeftStickPOV`/`RightStickPOV`. NeoXR-Input.exe now also prints hat positions.
- Pressed buttons stay lightly highlighted; independent model LED colors come from the baked materials. This does not synchronize physical hardware LEDs or SimPro RGB settings.
- LeftPaddleButton and RightPaddleButton animate the named left/right shifters. Set each to its zero-based DirectInput index on ButtonDevice. -1 disables that motion.
- LeftClutchButton and RightClutchButton provide the same optional digital animation for clutch levers. For analog levers set `LeftClutchAxis`/`RightClutchAxis` plus their `Rest`/`Full` readings from NeoXR-Input; the lever then follows its travel up to `ClutchAngleDegrees`, without a click.
- Paddle hinge locations are approximated from the parts' inner edges, and require visual calibration for exact motion. PaddleAngleDegrees=0 disables all paddle movement. The converter stores pivots in the binary asset; wheel-parts.json documents them.
- Brightness controls the custom model's output. The renderer uses fixed studio lighting, not iRacing's cockpit lights. Procedural red paint was approximated during GLB export. The result will differ from the Blender preview's lighting.

The original source's two detached pieces are excluded. Exact body-label texture changes, RPM telemetry and virtual hands are not added in this release.

## Model pipeline

The shipped runtime assets are ready for installation; Python or Blender is not needed to use them. The DLL loads a prepared `wheel.neo` plus three PNG atlases from AssetDirectory. It does not parse arbitrary GLB files at frame time.

For another export of the prepared baked model, run Python 3.9+:

```powershell
python tools/convert_glb.py C:\path\NeoXR-wheel.glb assets
```

The converter has no external Python dependencies. It supports this static, opaque triangle model with embedded PNG atlases and node transforms, and rejects unsupported texture layouts or accessor types. It preserves control identity and bakes node transforms into the vertices. Other Blender/glTF features may require an extended converter. The original .blend and portable .glb are in the separately supplied NeoXR-wheel-assets.zip.

## Manual build / first installation

Requirements: Visual Studio 2022 or newer with Desktop development with C++, Windows SDK, CMake 3.20+, and internet for the first Khronos OpenXR header download.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release
```

The complete package is in build/package, including assets. A new installation must configure device indices, set Enabled=1, and register its NeoXR.json using Register.ps1 in elevated 64-bit PowerShell. An update to the previously registered package does not require that step. Changing the package's location requires unregistering from the old location and registering from the new location.

## Test on Quest 3

Check both eyes show the complete wheel; steering sign and angle agree with the hardware; F8/F9 still work; and each configured button moves its corresponding cap. Configure and test the shift paddles separately. Compare frame time against Model=placeholder or Enabled=0 before using the model for a long session. GPU performance has not been measured here.

If the overlay is absent, check NeoXR.log beside the DLL. A shader or asset-load failure is logged and disables this session's overlay. Input loss omits the overlay until the same device can be polled again. Report the log or compiler error text if this new version fails.

## Existing limits

The layer composites over the game and cannot remove its wheel or occlude against its cockpit. Hide the original wheel in iRacing through its supported settings where possible. This remains a single-instance, single rendered session, D3D11 prototype. Device GUID persistence, a settings GUI, INI hot reload, runtime recenter-event handling, D3D12/Vulkan and saved tracking-space calibration are not implemented. F8 must follow runtime recentering.

Unregister.ps1 removes this package's manifest registration. Enabled=0 disables its rendering on the next session; NEOXR_DISABLE=1 in the application's inherited environment prevents loading. No force-feedback commands are issued. If the game blocks the layer, stop; no bypass is included.

## Technical references

- OpenXR loader: https://registry.khronos.org/OpenXR/specs/1.1/loader.html
- glTF transforms/materials: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
- WIC texture loading: https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-resources-textures-how-to

Independently authored prototype, not affiliated with SIMAGIC, Meta or iRacing.
