# Native integration status

## Device validation — 2026-11-25

Diagnostic APK successfully read the user's Xbox image on Retroid Pocket 6.

- Title ID: `56550039`
- Region mask: `7`
- XBE size: `5197824` bytes
- SHA-256: `4f233ee49f8e675d88ed54075ad4e270387798435e7a3f4a66836d0ec0e768ee`

This is an observed fingerprint, not an upstream compatibility certification.

## Native integration gate

Upstream: https://github.com/matthewfcarlson/thunderclap

Use a local upstream checkout and a locally extracted original disc. Do not commit game files, generated data or third-party source without permission. The upstream data inventory requires the XBE, recovered function CSV, RTTI JSON and source tree. Its Android build requires the NDK, SDL3, a clang-probe snapshot and binding map; FFmpeg is needed for movies. See upstream docs/data.md and docs/native-android.md.

Before a native game build: establish the upstream reference executable fingerprint, verify source availability and usage terms, generate local data, compile and link ARM64 API 29, inspect missing symbols and test startup. A successful diagnostic APK does not establish game playability. Upstream documents remaining startup and graphics limitations.
