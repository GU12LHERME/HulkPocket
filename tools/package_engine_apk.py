#!/usr/bin/env python3
"""Prepare or locally package Thunderclap's native Android APK.

This deliberately uses Thunderclap's SDLActivity and packaging tool. It never
bundles an ISO, XBE or game data, and does not modify the HulkPocket diagnostic
APK. The caller must supply their own locally built engine and dependencies.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


def is_elf64_aarch64(path: Path) -> bool:
    if not path.is_file():
        return False
    with path.open("rb") as f:
        h = f.read(20)
    return (len(h) == 20 and h[:4] == b"\x7fELF"
            and h[4] == 2 and h[5] == 1
            and int.from_bytes(h[18:20], "little") == 183)


def requirements(upstream: Path, engine: Path, ffmpeg: Path):
    return {
        "upstream_packager": (upstream / "tools/android_apk.py").is_file(),
        "upstream_sdl_activity": (upstream / "src/platform/sdl/android/java/org/thunderclap/hulkud/HulkActivity.java").is_file(),
        "sdl3_java_sources": (upstream / "build/third_party/src/SDL/android-project/app/src/main/java/org/libsdl/app/SDLActivity.java").is_file(),
        "native_engine_arm64": is_elf64_aarch64(engine),
        "ffmpeg_avcodec_arm64": is_elf64_aarch64(ffmpeg / "lib/libavcodec.so"),
        "ffmpeg_avformat_arm64": is_elf64_aarch64(ffmpeg / "lib/libavformat.so"),
        "ffmpeg_avutil_arm64": is_elf64_aarch64(ffmpeg / "lib/libavutil.so"),
        "ffmpeg_swresample_arm64": is_elf64_aarch64(ffmpeg / "lib/libswresample.so"),
    }


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--upstream", required=True, type=Path, help="local Thunderclap checkout")
    p.add_argument("--engine", required=True, type=Path, help="locally compiled ARM64 libmain.so")
    p.add_argument("--ffmpeg", required=True, type=Path, help="local Android FFmpeg install prefix")
    p.add_argument("--out", required=True, type=Path, help="output APK, preferably build/engine/hulk.apk")
    p.add_argument("--execute", action="store_true", help="actually invoke upstream APK packager")
    args = p.parse_args(argv)
    upstream, engine, ffmpeg, out = (
        args.upstream.resolve(), args.engine.resolve(), args.ffmpeg.resolve(), args.out.resolve()
    )
    checks = requirements(upstream, engine, ffmpeg)
    report = {
        "ready_to_package": all(checks.values()),
        "checks": checks,
        "executed": False,
        "warning": "Packaging is not evidence that the game boots, renders or is playable."
    }
    print(json.dumps(report, indent=2), flush=True)
    if not report["ready_to_package"]:
        return 1
    if not args.execute:
        print("All file checks passed. Add --execute for a local packaging attempt.")
        return 0

    # Do not pass the original disc to the packaging tool.
    out.parent.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["HULK_ANDROID_FFMPEG"] = str(ffmpeg)
    command = [
        sys.executable, str(upstream / "tools/android_apk.py"),
        str(engine), "--out", str(out),
    ]
    result = subprocess.run(command, cwd=upstream, env=env, check=False)
    if result.returncode:
        print(f"Upstream APK packager failed with exit code {result.returncode}", file=sys.stderr)
        return result.returncode
    if not out.is_file():
        print("Upstream packager exited successfully but output APK was not created.", file=sys.stderr)
        return 1
    print(f"Native engine APK generated locally: {out}")
    print("This is a non-playable experimental build until device tests prove otherwise.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
