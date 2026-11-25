# HulkPocket: XboxXBE-Recomp Android feasibility (experimental branch)

Status: **not playable**. This branch preserves the current Thunderclap-based Android
diagnostic and evaluates a separate route using
[Hulk Recompiled](https://github.com/lookingcoding/hulk-ultimate-destruction-recomp-xbox-ver-)
and [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp).
No upstream game code, executable, ISO or generated source is copied here.

## Verified upstream implementation facts

- Hulk Recompiled is an **experimental playable Windows x64 build**, with
  documented movie, title screen, controls and junkyard-tutorial gameplay.
  The upstream STATUS.md explicitly does **not** claim a verified full campaign.
- The upstream CMakeLists.txt invokes `add_xbe_image` from the XboxXBE-Recomp
  SDK and compiles the game's image from `assets/default.xbe`.
  `build.bat` delegates to a PowerShell Windows build pipeline.
- The original Xbox image identifier in `config/game.toml` is
  `title = 0x56550039`; the SDK target is XDK 5849.
  Equal title IDs **do not** establish matching executable revisions.
- The Windows frontend includes an executable loader (linked against
  `shell32`) and a DLL output, `HulkRecompiled.dll`.
- `src/game/fibers.cpp` uses Win32 `CreateFiberEx`,
  `ConvertThreadToFiberEx`, `SwitchToFiber`, `DeleteFiber`,
  and `GetCurrentFiber`; simply using an Android compiler would not
  preserve the game's cooperative thread continuation semantics.
- Upstream's D3D11/D3D12 renderer depends on Direct3D and Windows
  graphics APIs. SDL3 gamepad availability is used by input,
  but does **not** imply the engine has an Android SDL video backend.

These observations come from reading upstream `README.md`, `STATUS.md`,
`CMakeLists.txt`, `config/game.toml`, `src/game/fibers.cpp` and
`src/common/game_info.cpp`.

## Critical-path Android gates

1. **Recompiler portability**: establish an x86-to-ARM64 generated
   source compilation path that produces an AArch64 ELF shared object,
   without requiring a Windows DLL host. The SDK currently targets the
   Windows-native app; this is not solved by adding an Android Gradle task.
2. **Host runtime**: replace or port Win32 filesystem, threading, timing,
   exception/SEH, fiber-stack switching, executable and dynamic
   symbol-loading behavior. Specifically test fiber suspend/resume of a
   nested C++ continuation rather than stubbing out the calls.
3. **Graphics**: implement a tested Android Vulkan or OpenGL ES backend
   translating the Xbox D3D8-style render operations, shaders and
   texture/palette paths. Do not claim rendering success from a
   blank window.
4. **Audio, controller, disc and save I/O**: adapt host APIs to Android;
   verify the original disc's file layout and proper storage permissions,
   gamepad buttons/axes and real audio playback on RP6.
5. **Local game-code generation**: verify the exact local `default.xbe`
   revision, generate recompiled C++ without uploading copyrighted
   output to public Actions, then compile and link a real motor image.
6. **Device acceptance**: only call the port *playable* after RP6 evidence
   of title screen, rendered tutorial, controls, audio, saving and
   sustained gameplay without a runtime crash.

## APK delivery

The public repository currently builds **HulkPocket 0.2.0 diagnostic APK**
through `.github/workflows/android-apk.yml`. It verifies the ISO and
loads a tiny JNI ARM64 bridge, but it explicitly reports that the native
game motor is **not integrated**. This artifact must not be labeled
`Hulk Ultimate Destruction Android playable`.

An actual playable APK cannot currently be produced from the public
sources alone. CI cannot substitute for the missing ARM64 runtime and
renderer nor privately generated game-derived inputs.

Keep this work on `experiment-xbe-recomp-android` until a real
AArch64 engine can compile, link and boot on the RP6. Avoid disrupting
the currently working diagnostic APK on `main`.
