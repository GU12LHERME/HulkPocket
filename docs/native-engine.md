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
