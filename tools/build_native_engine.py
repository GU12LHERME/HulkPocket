#!/usr/bin/env python3
"""Run Thunderclap's real Android ARM64 compile, link and SDL3 APK packaging locally.

Requires a locally generated clang-probe snapshot and binding map, an Android
NDK, and the upstream SDL3/FFmpeg app builds. No game assets are uploaded.
This does not repair Thunderclap's unfinished native game startup.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

from check_game_version import verify as verify_game_version


def make_plan(upstream, snapshot, bindings, ndk, sdl, ffmpeg, output_dir, apk, jobs):
    checks = {
        "upstream_native_compiler": (upstream / "tools/native_elf.py").is_file(),
        "upstream_packager": (upstream / "tools/android_apk.py").is_file(),
        "upstream_crt_table": (upstream / "src/platform/sdl/crt/native_crt_table.h").is_file(),
        "generated_snapshot": (snapshot / "snap/manifest.json").is_file(),
        "generated_bindings": bindings.is_file(),
        "ndk_clang": any(
            (ndk / "toolchains/llvm/prebuilt" / host / "bin/clang").is_file()
            for host in ("linux-x86_64", "darwin-x86_64", "darwin-arm64")
        ),
        "sdl_app_static_library": (sdl / "lib/libSDL3.a").is_file(),
        "ffmpeg_avcodec": (ffmpeg / "lib/libavcodec.so").is_file(),
        "ffmpeg_avformat": (ffmpeg / "lib/libavformat.so").is_file(),
        "ffmpeg_avutil": (ffmpeg / "lib/libavutil.so").is_file(),
        "ffmpeg_swresample": (ffmpeg / "lib/libswresample.so").is_file(),
    }
    if checks["generated_snapshot"]:
        try:
            manifest = json.loads((snapshot / "snap/manifest.json").read_text())
            checks["generated_snapshot"] = isinstance(manifest, (dict, list)) and bool(manifest)
        except (ValueError, OSError):
            checks["generated_snapshot"] = False
    if checks["generated_bindings"]:
        try:
            data = json.loads(bindings.read_text())
            checks["generated_bindings"] = isinstance(data, (dict, list)) and bool(data)
        except (ValueError, OSError):
            checks["generated_bindings"] = False

    env = os.environ.copy()
    env.update({
        "HULK_NDK": str(ndk),
        "HULK_CLANG_PROBE_DIR": str(snapshot),
        "HULK_NATIVE_ELF_DIR": str(output_dir),
        "HULK_ANDROID_SDL": str(sdl),
        "HULK_ANDROID_FFMPEG": str(ffmpeg),
        "HULK_NATIVE_CRT_TABLE": str(upstream / "src/platform/sdl/crt/native_crt_table.h"),
    })
    py = str(upstream / "build/venv/bin/python")
    if not Path(py).is_file():
        py = sys.executable
    native = str(upstream / "tools/native_elf.py")
    commands = [
        [py, native, "compile", "--target", "android-arm64", "--jobs", str(jobs)],
        [py, native, "link", "--target", "android-arm64", "--ld", "--bindings", str(bindings)],
    ]
    image = output_dir / "link/libmain.so"
    package = [
        sys.executable, str(Path(__file__).resolve().parent / "package_engine_apk.py"),
        "--upstream", str(upstream), "--engine", str(image),
        "--ffmpeg", str(ffmpeg), "--out", str(apk), "--execute",
    ]
    return checks, env, commands, package, image


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("upstream", "snapshot", "bindings", "ndk", "sdl", "ffmpeg", "out"):
        parser.add_argument("--" + key, type=Path, required=True)
    parser.add_argument("--apk", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--xbe", type=Path, help="local extracted default.xbe from the tested ISO")
    parser.add_argument("--execute", action="store_true", help="execute upstream build (default: preflight only)")
    args = parser.parse_args(argv)
    if args.jobs < 1 or args.jobs > 64:
        parser.error("--jobs must be between 1 and 64")
    paths = {k: getattr(args, k).expanduser().resolve() for k in
             ("upstream", "snapshot", "bindings", "ndk", "sdl", "ffmpeg", "out", "apk")}
    if paths["apk"].suffix.lower() != ".apk":
        parser.error("--apk must end in .apk")

    if args.execute and args.xbe is None:
        parser.error("--execute requires --xbe to verify the local disc executable")
    xbe_check = None
    if args.xbe is not None:
        try:
            xbe_check = verify_game_version(args.xbe.expanduser().resolve())
        except (OSError, ValueError) as exc:
            print(json.dumps({"xbe_verification": {"error": str(exc)}}), flush=True)
            return 1
        if not xbe_check["matches_rp6_executable"]:
            print(json.dumps({"xbe_verification": xbe_check}, indent=2), flush=True)
            return 1

    checks, env, commands, package, image = make_plan(
        paths["upstream"], paths["snapshot"], paths["bindings"], paths["ndk"],
        paths["sdl"], paths["ffmpeg"], paths["out"], paths["apk"], args.jobs
    )
    # Never dump the bindings or game code into logs.
    print(json.dumps({"preflight": checks, "ready": all(checks.values()),
                      "execute_requested": args.execute,
                      "xbe_matches_tested_rp6_executable": (xbe_check["matches_rp6_executable"] if xbe_check else None),
                      "note": "Native compilation requires locally generated game-derived inputs."}, indent=2),
          flush=True)
    if not all(checks.values()):
        return 1
    if not args.execute:
        print("Preflight passed. Run again with --execute to compile, link and package.")
        return 0
    paths["out"].mkdir(parents=True, exist_ok=True)
    for command in commands:
        print("Running upstream stage:", command[2], flush=True)
        rc = subprocess.run(command, cwd=paths["upstream"], env=env, check=False).returncode
        if rc:
            print("Native build failed; no APK packaged.", file=sys.stderr)
            return rc
    if not image.is_file():
        print("Native linker did not produce libmain.so; no APK packaged.", file=sys.stderr)
        return 1
    # The upstream packager handles the required SDL3 activity and FFmpeg libs.
    return subprocess.run(package, cwd=Path(__file__).resolve().parents[1],
                          env=env, check=False).returncode

if __name__ == "__main__":
    raise SystemExit(main())
