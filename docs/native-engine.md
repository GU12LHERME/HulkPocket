# Native engine integration — preparation (not a playable build)

The diagnostic APK has successfully read the user's original Xbox ISO on Retroid Pocket 6:

- Title ID: `56550039`
- XBE length: `5197824` bytes
- XBE SHA-256: `4f233ee49f8e675d88ed54075ad4e270387798435e7a3f4a66836d0ec0e768ee`

This identifies the tested executable, **not** a verified match with Thunderclap's input. No upstream reference hash has been established.

## Upstream baseline

Thunderclap documents an Android ARM64 `libmain.so` and SDL3 APK in `docs/native-android.md` section 16. The upstream Android APK currently stops during game startup, lacks graphics, and has not been validated on physical hardware. An Android native library cannot be built merely from the ISO: Thunderclap's native pipeline also requires generated IR/object snapshots and bindings.

## Local integration gate

Keep the upstream checkout and legally owned disc files **outside** this public repository. Do not publish an ISO, XBE, extracted game data, generated decompilation, or a third-party native binary without verifying redistribution rights.

Run:

```sh
python3 tools/native_preflight.py --upstream /path/to/thunderclap --lib /path/to/libmain.so --data /path/to/extracted-disc --json
```

The command checks upstream Android build entry points, ELF64/AArch64 library headers, and the presence of local `default.xbe`. It does **not** claim that the library is runnable, linked to this Android app, or that the disc version matches upstream.

## Remaining implementation

1. Verify upstream licensing/permission before copying any code into HulkPocket.
2. Reproduce Thunderclap's full Android native build with a compatible local generated snapshot and bindings.
3. Verify game version compatibility and correct unresolved game functions / 64-bit pointer issues.
4. Package the tested native library and SDL3 activity in a separate experimental APK.
5. Validate initialization, rendering, input, audio, saves and device stability.

The current `com.hulkpocket.diagnostic` app is intentionally separate from any unverified native engine. Do not replace it with a nonfunctional black-screen APK.

## Experimental engine APK packaging path

HulkPocket now provides `tools/package_engine_apk.py` as an integration wrapper
around **Thunderclap's own** `tools/android_apk.py`. This is intentionally separate
from the verified HulkPocket 0.2.0 diagnostic APK; the engine requires the upstream
SDL3 activity and can crash before any image is drawn.

Requirements, produced **locally** using the upstream instructions in
`docs/native-android.md` section 16:

- A complete Thunderclap checkout with its fetched SDL3 Android Java sources.
- A real, locally linked ARM64 `libmain.so`, built with the SDL3 **app** configuration;
  a placeholder ELF or the HulkPocket JNI test library is not usable.
- The Android FFmpeg installation prefix containing ARM64 `libavformat.so`,
  `libavcodec.so`, and `libavutil.so`.
- Android SDK build-tools 35, platform 35 and Java 17+ for the upstream packager.

First inspect input readiness without generating an APK:

```sh
python3 tools/package_engine_apk.py \
  --upstream /path/to/thunderclap \
  --engine /path/to/libmain.so \
  --ffmpeg /path/to/ffmpeg-android-arm64/install \
  --out build/engine/hulk.apk
```

Add `--execute` to invoke the upstream packager after all checks pass.
The wrapper deliberately does **not** upload or bundle the game's files.
The engine's own activity expects extracted game data in its app-specific
`data/` directory, as described by the upstream. A raw ISO selection in
the HulkPocket diagnostic activity does not install that data for the engine.

**Important:** This packaging wrapper is not the upstream compiler, does not
create the missing intermediate generated objects and binding map, and does
not make Thunderclap playable. Validate permissions before redistributing any
upstream-derived APK or binary.

## Full native build orchestration

`tools/build_native_engine.py` joins the upstream compile, link and APK
packaging commands into a **local-only** workflow. This is different from
`tools/package_engine_apk.py`, which requires an already linked `libmain.so`.

Prepare these local inputs using **Thunderclap's** documented toolchain:
- `THUNDERCLAP`: full upstream checkout with Python dependencies installed.
- `SNAPSHOT`: directory containing a generated `snap/` tree from the
  upstream clang-probe pipeline, consistent with the current source tree.
- `BINDINGS`: compatible binding map (`bindings.json`) from the Wine proof
  build of that **same** source tree.
- `NDK`: Android NDK installation with clang.
- `SDL_APP`: Thunderclap's SDL3 **app** build, containing `lib/libSDL3.a`.
- `FFMPEG`: Thunderclap's Android FFmpeg install prefix with
  `libavformat.so`, `libavcodec.so` and `libavutil.so`.

Check the plan without compiling:

```sh
python3 tools/build_native_engine.py \
  --upstream /path/to/thunderclap \
  --snapshot /path/to/clang-probe \
  --bindings /path/to/bindings.json \
  --ndk /path/to/android-ndk \
  --sdl /path/to/sdl-android-arm64-app \
  --ffmpeg /path/to/ffmpeg-android-arm64/install \
  --xbe /path/to/extracted/default.xbe \
  --out build/native-android-arm64 \
  --apk build/engine/hulk.apk
```

Only after all input checks pass, add `--execute`. That executes:

1. `native_elf.py compile --target android-arm64 --jobs 2`
2. `native_elf.py link --target android-arm64 --ld --bindings ...`
3. `package_engine_apk.py --execute` invoking the upstream
   `android_apk.py` to produce an isolated native SDL3 APK.

This process may take considerable CPU time and storage. It has **not** been
executed for HulkPocket: the generated snapshot, binding map, SDK
dependencies and local game-derived data are not present in this repo or
available to its public GitHub Actions runner. The test workflow exercises
the orchestration and input checks with synthetic data; it does not build
Thunderclap's actual game engine.

The current upstream Android link may contain numerous deliberately
stubbed unresolved functions and stop before gameplay; a successful
link/package does not constitute a working port. The user's verified
Title ID / SHA-256 do not establish identity with Thunderclap's build input.

A public Actions workflow must not be made to download copyrighted disc
contents or publish the generated game binary. Do not conflate the isolated
native SDL APK with the tested HulkPocket diagnostic application.

## Executable identity gate (RP6 test)

The original diagnostic on the Retroid Pocket 6 identified Xbox Title ID
`56550039` and XBE SHA-256
`4f233ee49f8e675d88ed54075ad4e270387798435e7a3f4a66836d0ec0e768ee`
(5,197,824 bytes).

`tools/check_game_version.py /path/to/extracted/default.xbe` validates a **local**
copy against those exact values; no game bytes are sent anywhere.
`tools/build_native_engine.py --execute` now requires `--xbe
/path/to/extracted/default.xbe`, and aborts if it differs. A different valid
revision of the game needs separate compatibility analysis before the
reference is changed.

This check **does not prove** that Thunderclap's generated clang-probe
snapshot or binding map was produced from this same XBE. That provenance
must be established when the upstream build artifacts are generated.
It also does not solve Thunderclap's missing functions or graphical startup.

## Android SDL3 app dependency CI (isolated)

The `.github/workflows/thunderclap-sdl3.yml` workflow checks out the
upstream Thunderclap project into the GitHub runner's temporary directory,
fetches SDL3 through the upstream pinned fetcher, and builds
`libSDL3.a` for Android ARM64 with the upstream
`native_android_sdl.py --config app` script. The workflow validates
the resulting static archive and headers and records the upstream commit
and archive hash in its logs. It deliberately does not upload the upstream
library or user game files as public artifacts.

This is a genuine build attempt for the Android platform **dependency**,
not for game code. It does not create an executable game engine or solve
the clang-probe snapshot and binding-map requirements.

As of adding this workflow, its first GitHub Actions result has **not**
been independently verified; a passing workflow is required before
marking the SDL3 build as validated.

## Android FFmpeg/dav1d dependency CI

The isolated `.github/workflows/thunderclap-ffmpeg.yml` workflow now attempts
an actual Android ARM64 build of the current upstream's multimedia stack:

- Clones Thunderclap into a temporary runner directory (records commit SHA).
- Installs Android NDK r28c, Clang, Meson, pkgconf and Ninja.
- Runs upstream `tools/wine_ffmpeg.py build --target android-arm64`.
- Upstream fetches hash-pinned FFmpeg and dav1d sources and builds dav1d
  before FFmpeg, with Bink, AV1, Opus and Matroska support.
- Checks that `libavcodec.so`, `libavformat.so`, `libavutil.so` and
  `libswresample.so` are nonempty ELF64/AArch64 libraries.

This CI intentionally publishes no FFmpeg or game binary artifacts. It does
not upload copyrighted disc content or generate the native game engine.
The first result must pass in GitHub Actions before the dependency is
considered verified.

Note: The current Thunderclap FFmpeg Android build contains a static dav1d
dependency in libavcodec, and libswresample is a required fourth shared
library. The local packaging tool uses the upstream packager, which includes
all *.so files from the selected FFmpeg prefix.
