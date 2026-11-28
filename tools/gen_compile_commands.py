#!/usr/bin/env python3
"""Write a repo-root compile_commands.json for clangd from the Gradle NDK build.

Android Gradle's CMake run emits android/app/.cxx/<variant>/<hash>/arm64-v8a/
compile_commands.json with exact NDK flags for the APK sources. This copies
those entries and adds synthesized entries (same NDK compiler, sysroot and
include paths, C++20) for every other native/ and third_party C++ source,
such as tests and host tools, so clangd stops guessing flags for them.

Run after `gradle -p android assembleDebug` (any variant). The output is
machine-specific and ignored by git.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import shlex
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE_GLOBS = ("native/**/*.cpp", "third_party/xboxxberecomp/**/*.cpp",
                "third_party/xboxxberecomp-reverb/**/*.cpp", "tools/xbe-recompiler-host/**/*.cpp")
EXTRA_INCLUDES = ("native", "third_party/xboxxberecomp", "android/app/src/main/cpp/sdk",
                  "third_party/scda-vulkan/src")


def newest_database() -> pathlib.Path:
    candidates = sorted((ROOT / "android/app/.cxx").glob("*/*/arm64-v8a/compile_commands.json"),
                        key=lambda p: p.stat().st_mtime, reverse=True)
    if not candidates:
        raise FileNotFoundError("No Gradle compile_commands.json; run gradle -p android assembleDebug first")
    return candidates[0]


def template_args(entries: list[dict]) -> tuple[list[str], str]:
    preview = next((e for e in entries if e["file"].replace("\\", "/").endswith("cpp/preview.cpp")), entries[0])
    args = preview.get("arguments") or shlex.split(preview["command"].replace("\\", "/"))
    kept: list[str] = []
    skip_next = False
    for arg in args:
        if skip_next:
            skip_next = False
            continue
        if arg in ("-o", "-c"):
            skip_next = True
            continue
        if arg.startswith("-std="):
            continue
        kept.append(arg)
    for include in EXTRA_INCLUDES:
        flag = "-I" + (ROOT / include).as_posix()
        if flag not in kept:
            kept.append(flag)
    kept.append("-std=gnu++20")
    return kept, preview["directory"]


def generate(output: pathlib.Path) -> int:
    database = newest_database()
    entries = json.loads(database.read_text(encoding="utf-8"))
    known = {pathlib.Path(e["file"]).resolve() for e in entries}
    base, directory = template_args(entries)
    added = 0
    for pattern in SOURCE_GLOBS:
        for source in sorted(ROOT.glob(pattern)):
            resolved = source.resolve()
            if resolved in known:
                continue
            entries.append({
                "directory": directory,
                "file": resolved.as_posix(),
                "arguments": [*base, "-c", resolved.as_posix()],
            })
            known.add(resolved)
            added += 1
    output.write_text(json.dumps(entries, indent=1) + "\n", encoding="utf-8")
    print(f"{output}: {len(entries)} entries ({added} synthesized) from {database.relative_to(ROOT)}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0] if __doc__ else None)
    parser.add_argument("--output", type=pathlib.Path, default=ROOT / "compile_commands.json")
    args = parser.parse_args()
    try:
        return generate(args.output)
    except FileNotFoundError as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
