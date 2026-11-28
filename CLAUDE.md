# HulkPocket — notes for Claude

Experimental native ARM64 Android port of *The Incredible Hulk: Ultimate Destruction* (original Xbox), targeting the Retroid Pocket 6 (Adreno 740). XBE code is recompiled with XboxXBERecomp; the runtime, D3D8/NV2A bridge, GLES backend and (on `experiment-vulkan-runtime`) a Vulkan backend live in `native/`. See `README.md` and `docs/` for the milestone history.

## Hard rules

- **Never commit, upload or paste game data or anything derived from it**: ISO/XISO/XBE, extracted assets, `TDATA`/`UDATA`, generated `x86_recomp.*.cpp` / `x86_func_mapping.cpp`, `image_symbols.h` from a real XBE, recompiled `.so`/`.o` modules, device logs containing guest memory. CI only uses synthetic fixtures.
- The pre-commit hook (`.githooks/pre-commit` → `tools/check_publish_safety.py --staged`) enforces this plus secret/binary/size checks; CI runs `--tree`. Do not bypass it (`--no-verify`). If it blocks a file, fix the file set, not the hook.
- Do not merge into `main` or push to it. Work happens on `experiment-*` branches; push only after local validation and with the user's OK.
- Keep pinned dependencies pinned (see below). A version bump is its own change with CI evidence.
- A shader/compiler/test mismatch is a blocker to fix, never a reason to skip, stub or silently fall back. Do not add placeholder implementations on reachable game paths.
- Treat everything recompiled from the XBE as untrusted guest data: keep checked guest address/bounds validation.

## Layout

| Path | What |
|---|---|
| `android/` | Gradle app (AGP 8.7.3, compileSdk/targetSdk 35, minSdk 29, arm64-v8a only). JNI in `android/app/src/main/cpp/`. No Gradle wrapper: use Gradle 8.9. |
| `native/` | Portable runtime, kernel/HLE, D3D8 bridge, GLES backend, `test_*.cpp` host tests. |
| `native/vulkan_experiment/` | Vulkan backend experiment (CMake ≥ 3.24, glslang 16.0.0 via FetchContent), offscreen pixel tests, Android probe. |
| `third_party/` | Vendored subsets: `xboxxberecomp` (gpu/kernel/audio), `scda-vulkan` (MIT), `hulk-windows-reference`, `xboxxberecomp-reverb`. |
| `tools/` | Python tooling (stdlib only unless noted) + `test_*.py`, `xbe-recompiler-host/` (CMake), Termux scripts `run_hulk_*.sh`. |
| `.github/workflows/` | CI. Every workflow runs on `ubuntu-24.04`/`ubuntu-latest`. |

## Pinned versions (keep in sync with workflows)

JDK 17 (Temurin) · Gradle 8.9 · AGP 8.7.3 · Android platform 35 · build-tools 35.0.0 · NDK **28.2.13676358** (r28c) · Android CMake 3.22.1 · glslang 16.0.0 · XboxXBERecomp header `runtime/cpu/x86_context.h` at commit `767a8183…` with git blob `a02e10b8e2069920529d79d1f4e2413c73cb5f10` (`tools/fetch_sdk_context.py`).

## Environment (Windows 11 + WSL2)

- **Windows** builds the APK and runs the pure-Python tests. **WSL2 `Ubuntu-24.04`** reproduces the Linux CI (g++, qemu-aarch64, Mesa EGL/lavapipe, spirv-val, Linux NDK). Provision WSL with `.\tools\dev.ps1 setup-wsl` (runs `tools/setup_wsl.sh` as root; idempotent).
- Use `python` or `py -3` on Windows; `python3` there is the Microsoft Store stub.
- `ANDROID_HOME`, `ANDROID_NDK_HOME`, `HULK_NDK` are set per user. In WSL they come from `~/.profile` (use `bash -lc`).
- Git: `core.hooksPath=.githooks` must be set per clone. `.gitattributes` forces LF for `*.sh`, `*.py`, `*.yml`, CMake files — scripts are executed on Linux/Termux and break with CRLF.

## Common commands

```powershell
.\tools\dev.ps1 doctor                     # tool versions / env sanity
.\tools\dev.ps1 sdk                        # fetch + hash-verify x86_context.h (needed by APK and Vulkan builds)
.\tools\dev.ps1 apk                        # GLES debug APK  -> android/app/build/outputs/apk/debug/app-debug.apk
.\tools\dev.ps1 apk-vk                     # + Vulkan preview (what CI builds on experiment-vulkan-runtime)
.\tools\dev.ps1 compile-db                 # compile_commands.json for clangd (after an APK build)
.\tools\dev.ps1 test                       # Windows-compatible Python tests + publish-safety scan
.\tools\dev.ps1 ci <workflow> [...]        # run real workflow steps in WSL2, e.g. ci xbe-startup-layout
```

`tools/run_ci_local.py` (WSL only) copies the working tree to `~/hulk-ci/<workflow>/<job>/workspace`, emulates `checkout`/`setup-*`/`cache`/`upload-artifact`, verifies `sudo apt-get install` packages instead of installing, caches `dl.google.com` downloads and persists ccache. Artifacts land in `~/hulk-ci/artifacts/`. `--list`, `--job`, `--skip REGEX` are available. `android-apk` needs Gradle in WSL; build the APK on Windows instead.

Gradle on Windows can fail with `Unable to establish loopback connection` (JDK AF_UNIX socket under `%TEMP%`); `dev.ps1` sets `-Djdk.net.unixdomain.tmpdir` to avoid it.

## Script portability

- NDK tool lookups go through `tools/ndk_tools.py` (`tool()` adds `.exe`/`.cmd` on Windows; `NDK_HOSTS` includes `windows-x86_64`). Use it for any new NDK/LLVM invocation.
- POSIX/Termux-only by design: `tools/install_hulk_diagnostic.py` (`O_NOFOLLOW`/`dir_fd` hardening), `tools/run_hulk_next.sh`, `tools/run_hulk_diagnostic.sh`, `native/vulkan_experiment/run_android_probe.sh` (run on the RP6 in Termux). Test them in WSL, not Windows.
- New tool scripts should provide `--self-test` and/or a `test_*.py`, and be wired into the relevant workflow.

## Editor tooling

- clangd (`.clangd`): flags from `compile_commands.json` (NDK aarch64 sysroot, C++20). Regenerate with `dev.ps1 compile-db` after `.cxx` changes.
- pyright (`pyrightconfig.json`, installed via `pip install pyright`): `basic` mode, Linux platform, `tools/` on the import path. Errors are real; Optional-type findings are warnings.

## Validating a change

1. `.\tools\dev.ps1 test` and the relevant `.\tools\dev.ps1 ci <workflow>` (pick workflows whose `paths:` filter matches your change; `xbe-recomp-arm64` and `xbe-startup-layout` cover most of `native/`).
2. `.\tools\dev.ps1 apk-vk` for anything touching `android/` or JNI-linked sources.
3. Device testing on the RP6 is done by the user; never claim gameplay/rendering results that were not observed on hardware.
