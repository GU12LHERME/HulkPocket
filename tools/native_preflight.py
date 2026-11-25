#!/usr/bin/env python3
"""Read-only readiness check for a locally built Thunderclap Android engine."""
import argparse
import json
from pathlib import Path

EXPECTED_TITLE_ID = "56550039"
KNOWN_XBE_SHA256 = "4f233ee49f8e675d88ed54075ad4e270387798435e7a3f4a66836d0ec0e768ee"

def check(upstream: Path, native_lib: Path | None, data: Path | None):
    results = {
        "upstream_checkout": (upstream / "tools/native_elf.py").is_file(),
        "android_packager": (upstream / "tools/android_apk.py").is_file(),
        "android_sdl_builder": (upstream / "tools/native_android_sdl.py").is_file(),
        "android_ffmpeg_builder": (upstream / "tools/wine_ffmpeg.py").is_file(),
        "native_library_elf64_aarch64": False,
        "local_game_data": bool(data and (data / "default.xbe").is_file()),
    }
    if native_lib and native_lib.is_file():
        with native_lib.open("rb") as f:
            header = f.read(20)
        results["native_library_elf64_aarch64"] = (
            len(header) == 20 and header[:4] == b"\x7fELF"
            and header[4] == 2 and header[5] == 1
            and int.from_bytes(header[18:20], "little") == 183
        )
    return results

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--lib", type=Path)
    parser.add_argument("--data", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    results = check(args.upstream, args.lib, args.data)
    if args.json:
        print(json.dumps({"checks": results, "ready_for_packaging": all(results.values())}, indent=2))
    else:
        for name, passed in results.items():
            print(f"{'OK' if passed else 'MISSING'} {name}")
        print("NOTE: these checks do not prove gameplay or ABI compatibility.")
    return 0 if all(results.values()) else 1

if __name__ == "__main__":
    raise SystemExit(main())
