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
import struct
import sys


def is_elf64_aarch64(path: Path) -> bool:
    """Check ELF64 little-endian AArch64 shared-object structure.

    Rejects truncated files, non-shared ELF executables, absent program headers,
    and offsets that point beyond the file. Does not replace full loader testing.
    """
    if not path.is_file() or path.is_symlink():
        return False
    try:
        with path.open("rb") as f:
            header = f.read(64)
        size = path.stat().st_size
        if len(header) != 64 or size < 64:
            return False
        if header[:4] != b"\x7fELF" or header[4] != 2 or header[5] != 1 or header[6] != 1:
            return False
        kind, arch = struct.unpack_from("<HH", header, 16)
        if kind != 3 or arch != 183:   # ET_DYN, EM_AARCH64
            return False
        ehsize = struct.unpack_from("<H", header, 52)[0]
        phentsize, phnum = struct.unpack_from("<HH", header, 54)
        phoff = struct.unpack_from("<Q", header, 32)[0]
        if ehsize != 64 or phentsize != 56 or not phnum:
            return False
        if phoff < 64 or phoff + phentsize * phnum > size:
            return False
        # Require a loadable segment whose bytes fit inside the file.
        with path.open("rb") as f:
            f.seek(phoff)
            segments = f.read(phentsize * phnum)
        found_load = False
        for index in range(phnum):
            base = index * phentsize
            p_type = struct.unpack_from("<I", segments, base)[0]
            if p_type != 1:  # PT_LOAD
                continue
            offset, vaddr, paddr, filesz, memsz = struct.unpack_from("<QQQQQ", segments, base + 8)
            if filesz > memsz or offset > size or filesz > size - offset:
                return False
            if filesz:
                found_load = True
        return found_load
    except OSError:
        return False


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
