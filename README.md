# NeoXR 0.3.0 — desktop app and native wheel models

NeoXR is a Windows D3D11/OpenXR wheel overlay for iRacing. The desktop app configures devices, input bindings, placement, appearance and sound, with a live preview rendered by the same code as the in-game layer.

## Run and install

Run `NeoXR-Setup-0.3.0.exe` to install or update. Close iRacing and the NeoXR app during this one-time DLL update. The installer adds a **NeoXR desktop icon** and a Start menu entry, installs to `C:\NeoXR`, and preserves existing settings. Once the new DLL is loaded, model and settings changes no longer need a game restart.

Double-click the desktop icon to open the app. Minimizing keeps it running in the taskbar. Closing the window exits the app; unapplied edits are discarded. **Apply live** saves a complete settings generation for the DLL to reload. The UI also reloads external INI changes while retaining pending edits. The DLL watches models independently inside iRacing, so closing the desktop app does not unload the game's API layer.

From source: `dotnet build setup\NeoXR.Setup.csproj -c Release`, then double-click `Open-Control-Center.cmd`. The launcher uses the INI in `C:\NeoXR` if present, otherwise this source folder's INI. The matching `NeoXR-InputBridge.dll` must be beside the EXE for live input and preview. Build/update scripts package it automatically.

## Choose a wheel and reload live

Open **Wheel & placement**, choose **custom**, then **Browse** for an `.fbx`, `.obj`, `.gltf` or `.glb` wheel. Click **Apply live**. The DLL imports the selected file at startup and whenever it or its referenced buffers, OBJ material file or textures change. Files are read and parsed on a worker thread. Replacement resources are committed together after a successful import/upload; partial saves and invalid models retain the last working wheel and report the error in `NeoXR.log`.

```ini
[Wheel]
Model=custom
ModelFile=assets/neo-x.gltf
```

Relative model paths resolve against the INI folder. Texture/buffer paths resolve against the model. The measured NEO X is shipped as `assets/neo-x.gltf` with `neo-x.bin` and three PNG atlases. Its measurement corrections are already baked into that glTF. **The importer never applies NEO X deformation to another wheel.** Node transforms, materials and mesh proportions come from the selected source; `WidthMm` scales the complete wheel uniformly for placement.

Author rigid triangle meshes with the steering origin at the hub, +Y up and +Z toward the driver. Name a mesh or its parent node to select an animation:

- `B1_push_btn` … `B8_push_btn`: eight push buttons.
- `left_shifter`, `right_shifter`: shift paddles.
- `left_clutch_lever`, `right_clutch_lever`: clutch levers.
- `B9_B10_rotary_knob`, `B11_B12_rotary_knob`: thumb rollers.
- `B37_B38_rotary_knob`, `B39_B40_rotary_knob`: rotary knobs.
- `joystick_left`, `joystick_right`: joysticks. The original `joistick_right` spelling remains supported.

Other meshes rotate with steering but have no individual control animation. Hinges and knob axes are estimated from each imported part's geometry. The loader supports rigid geometry and the UV map selected by each material (all textures in a material must use the same UV map), with base color, normal, metallic, roughness, occlusion and emission materials; it does not evaluate arbitrary shader graphs or skeletal animation. Bake procedural materials first. Limits: 4 million vertices, 5 million triangles (15 million indices), 512 materials, 4096 meshes, 8192-pixel textures. More detailed models use more GPU memory and take longer to reload and render.

Overlay enable/disable, input devices, mappings, steering, placement, calibration, shortcuts, brightness, animation and sound settings reload during an existing D3D11 session. INI changes wait until mouse placement mode ends. `Process` and OpenXR layer registration select the process at launch and need a new game launch. Passthrough can be toggled live when the runtime supports it.

## Update the working installation

Close iRacing. Extract this source archive into a **new folder**, then open PowerShell in the extracted `NeoXR` folder and run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Build-Update.ps1
```

The working installation defaults to `C:\NeoXR`; pass `-InstalledPackage <folder>` if yours is elsewhere. The script builds x64 Release, runs tests, installs into the new source's build/package folder, backs up the old DLL and settings, then copies the new assets and DLL into the existing package. Your INI and registered manifest are retained. No re-registration is needed. It stops before updating if compilation or tests fail.

To place the wheel with the mouse, press **Tab** in iRacing. The wheel turns blue: left-drag moves it, right-drag or the scroll wheel moves it closer or farther. **M** switches to an orange size/rotate mode: left-drag resizes, right-drag up/down pitches and left/right yaws; each right-drag sticks to the direction it starts in, so release and drag again to change the other. **Esc** turns it green; **Enter** saves to NeoXR.ini and a second **Esc** cancels. While editing, NeoXR captures the mouse and the edit keys; five quick Esc presses release them. Change the key with `EditKey` and the speed with `EditSensitivity`.

### Matching the in-game wheel, per car

NeoXR reads which car you are driving from iRacing's telemetry (the read-only SDK shared memory) and keeps a separate alignment for each car:

1. In iRacing's graphics options, show the in-game steering wheel, without the virtual hands.
2. Press **Tab**. The wheel becomes see-through (`EditOpacity`).
3. Hold the wheel straight and use the mouse controls to line up NeoXR's wheel with the in-game wheel. Adjust its position, size, tilt and heading as needed.
4. Press **Esc**, then **Enter**. The log says `Car <name>: alignment saved`, and NeoXR.ini gains a line under `[Cars]`.
5. Hide the in-game wheel again. Whenever you drive that car, the wheel appears exactly there.

An alignment keeps its own calibration, so it stays fixed to the cockpit: **F8** then only changes the general placement used for cars without one, and iRacing's own VR recenter moves both wheels together. Ctrl+Shift nudges also save to the current car. To start over for a car, press **Tab**, **Esc**, then **Delete**. Without iRacing telemetry the general `X`/`Y`/`Z` placement is used, as before.

iRacing draws its virtual hands only with its own wheel, and NeoXR draws on top of the game's image, so it cannot hide the in-game wheel while keeping the hands. NeoXR.log records whether iRacing submits its depth buffer (`Game submits depth` or `Game does not submit depth`), which decides whether a future version can let the in-game hands show in front of this wheel.

Start iRacing in OpenXR, sit normally and press **F8**. F8 saves this calibration as `Calibration` in NeoXR.ini, so the wheel returns to the same place in later sessions. The wheel lives in iRacing's own tracking space, so when iRacing or the headset recenters, the wheel moves with the cockpit; set `RecenterKey` or `RecenterButton` to the control you bind to iRacing's VR recenter to recenter both together. **F9** hides/shows the wheel. **Ctrl+Shift+Down** moves the wheel 1 cm closer and **Ctrl+Shift+Up** moves it 1 cm away. **Ctrl+Shift+PageUp** tilts the top of the wheel 1° toward you and **Ctrl+Shift+PageDown** tilts it 1° away. The new values are saved as `Z` and `TiltDegrees` in NeoXR.ini. If the old INI has no Model entry, the default is now `custom`. To select explicitly, add these entries under the existing `[Wheel]` section:

```ini
Model=custom
ModelFile=assets/neo-x.gltf
Brightness=0.8
ButtonTravelMm=1.2
PaddleAngleDegrees=7
LeftPaddleButton=-1
RightPaddleButton=-1
LeftClutchButton=-1
RightClutchButton=-1
```

Keep your working Enabled, Process, SteeringDevice, ButtonDevice, SteeringAxis, RotationDegrees, Invert, WidthMm, X/Y/Z and TiltDegrees values.

To return to the original hardcoded visual, select `Model=placeholder` and apply live. This uses the original geometry and coloring through the same frame path. To restore the original DLL completely, close iRacing and copy NeoXR.dll from the timestamped backup folder into the package. If you have already customized this installation's asset files, the backup also contains those.

## Included model and controls

The runtime asset has 23 named parts, 599,691 triangles, and baked base-color, normal and metallic/roughness texture atlases. All steering geometry rotates together. The loader measures the transformed mesh's actual X extent, then scales it to your configured WidthMm. The prepared model is upright with the front facing positive Z.

- `Button0` through `Button7` now map to the model objects named **B1_push_btn through B8_push_btn**, respectively. In placeholder mode they retain the original visual order. These are zero-based DirectInput indices, not an assumption about SIMAGIC numbering. Verify each with NeoXR-Input.exe on the hub device.
- Every control has click feedback: a springy press animation and a synthesized click; the backlit push buttons also give a short highlight flash (`FlashStrength`). The click plays through the Windows default audio device (`ClickVolume`, or your own WAV via `ClickSoundFile`). Knob detents tick, buttons click and paddles clack.
- The rotary knobs turn one `KnobStepDegrees` per detent. The round B37/B38 and B39/B40 knobs turn freely; the B9/B10 and B11/B12 thumb rollers nudge and settle back because only their front arc is modeled. Defaults assume B9 is DirectInput button 8, and so on; set the `...KnobCW/CCW` keys and swap them if a knob turns backwards.
- The joysticks tilt by `StickAngleDegrees` and depress on push. Map them with `LeftStickUp/Down/Left/Right/Push` (and `RightStick...`) or, if they report as a hat switch, `LeftStickPOV`/`RightStickPOV`. NeoXR-Input.exe now also prints hat positions.
- Pressed buttons stay lightly highlighted; independent model LED colors come from the baked materials. This does not synchronize physical hardware LEDs or SimPro RGB settings.
- LeftPaddleButton and RightPaddleButton animate the named left/right shifters. Set each to its zero-based DirectInput index on ButtonDevice. -1 disables that motion.
- LeftClutchButton and RightClutchButton provide the same optional digital animation for clutch levers. For analog levers set `LeftClutchAxis`/`RightClutchAxis` plus their `Rest`/`Full` readings from NeoXR-Input; the lever then follows its travel up to `ClutchAngleDegrees`, without a click.
- Paddle hinge locations are approximated from the parts' inner edges, and require visual calibration for exact motion. PaddleAngleDegrees=0 disables all paddle movement. The importer derives pivots from the selected model; wheel-parts.json documents the supplied model.
- Brightness controls the custom model's output. The renderer uses fixed studio lighting, not iRacing's cockpit lights: a dim studio with softboxes and a broad panel behind the driver that metal, carbon and paint reflect (the carbon weave shows in those reflections), which stays still while the wheel turns. Procedural car paint is baked to plain colour, so its flakes are lost; the result will differ somewhat from Cycles renders of the original.

The original source's two detached pieces are excluded. Exact body-label texture changes, RPM telemetry and virtual hands are not added in this release.

## Optional model preparation

Direct import does not need Python or Blender. For the original NEO X Blender source, the optional baker unwraps and bakes its materials into glTF and texture atlases. Measurement correction is explicitly enabled only for that source:

```powershell
& "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -b C:\path\andvanced_2.blend --python tools\prepare_wheel.py -- build\wheel-prep --measured-neo-x
```

Omit `--measured-neo-x` for other wheel sources. The legacy GLB conversion helper now exports glTF and leaves geometry proportions unchanged; it is optional because the DLL reads GLB directly. No proprietary runtime mesh file is used.

## Manual build / first installation

Requirements are listed under Validation and build below.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release
dotnet build setup\NeoXR.Setup.csproj -c Release
```

The complete package is in build/package, including assets. A new installation must configure device indices, set Enabled=1, and register its NeoXR.json using Register.ps1 in elevated 64-bit PowerShell. An update to the previously registered package does not require that step. Changing the package's location requires unregistering from the old location and registering from the new location.

## Test on Quest 3

Check both eyes show the complete wheel; steering sign and angle agree with the hardware; F8/F9 still work; and each configured button moves its corresponding cap. Configure and test the shift paddles separately. Compare frame time against Model=placeholder or Enabled=0 before using the model for a long session. GPU performance has not been measured here.

If the overlay is absent, check NeoXR.log beside the DLL. Reload failures retain the last working model. Input loss leaves the wheel centered while NeoXR reconnects. Report the log or compiler error text if this new version fails.

### Lining the wheel up with the real one (passthrough)

In edit mode, **H** replaces the game with the room seen through the headset cameras, so the virtual wheel can be lined up with your real wheel using the usual mouse controls. Press H again to see the game, or leave edit mode (Esc then Enter saves) to switch passthrough off.

NeoXR enables OpenXR passthrough (`XR_FB_passthrough`) on iRacing's instance when the runtime offers it (`Passthrough=1`, the default). Over Link it needs **Passthrough over Meta Quest Link** in the Meta Horizon Link app (Settings, Beta).

## Existing limits

The layer composites over the game and cannot remove its wheel or occlude against its cockpit. Hide the game's wheel through its settings. This remains a single-instance, single rendered session D3D11 layer. Device GUID persistence, D3D12/Vulkan, hardware RGB synchronization, RPM LEDs and virtual hands are not implemented. If iRacing recenters internally without changing its tracking space, bind the same control with `RecenterKey`/`RecenterButton`.

`Unregister.ps1` removes this package's manifest registration. `Enabled=0` disables rendering live; `NEOXR_DISABLE=1` in the game's inherited environment prevents loading at launch. No force-feedback commands are issued.

## Validation and build

Build requirements: Visual Studio with Desktop development with C++, Windows SDK, CMake 3.20+, .NET SDK and .NET Framework 4.8 reference assemblies. The first build downloads pinned Assimp 5.4.3 and OpenXR headers. Assimp is statically linked with only FBX, OBJ and glTF importers enabled. See `THIRD-PARTY-NOTICES.txt`.

`ctest --test-dir build -C Release --output-on-failure` checks steering/core behavior, native model import, mesh naming, geometry preservation, dependency reload and partial-save recovery, D3D11 WARP rendering and failed replacement retention, configuration and placement behavior.

UI checks: `dotnet build tests\SetupTests.csproj -c Release`, then `build\setup-tests\net48\SetupTests.exe NeoXR.ini`. They verify staged edits, atomic save, discard, draft isolation, culture-independent numbers, section preservation, external reload with pending edits, page navigation, offline device selection and shortcut staging. Hardware and headset behavior still require verification in iRacing.

Build the installer with `powershell -NoProfile -ExecutionPolicy Bypass -File .\Build-Installer.ps1` (also requires Inno Setup 6).

Independently authored prototype, not affiliated with SIMAGIC, Meta or iRacing.

Steering zero: in the Steering page, set Zero offset (degrees), or hold the physical wheel centred and click Set current as zero. The virtual angle is the steering reading after inversion minus this offset. For a +3 degree reading, an offset of +3 shows zero. Apply live saves SteeringZeroOffsetDegrees and updates the desktop preview and VR wheel.

Live placement: Apply live updates each changed X/Y/Z, tilt, heading or width field in the active car's saved alignment as well as general placement. Other alignment fields, the car's calibration and other cars are preserved. The desktop preview also displays tilt and heading; headset position is only shown in VR.
