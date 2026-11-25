#!/usr/bin/env python3
"""Prepare XboxXBERecomp-generated C++ for ELF/Android linkage.

Runs AFTER upstream xbe_recomp produces its generated/ directory from
a user's lawful Xbox XBE. The tool does not contain or process game assets.
It rewrites only platform linkage declarations and the SDK x86_ops.h
Windows import/export macro block; instruction bodies are copied verbatim.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import shutil

EXPORT = '__attribute__((visibility("default")))'
TABLES = (
    "X86CodeBase", "X86CodeEnd", "X86ImageBase", "X86ImageEnd",
    "X86EntryPoint", "X86KernelThunk", "X86FuncMappings",
    "X86Continuations", "X86Symbols",
)
# Only definitions at the beginning of a generated mapping source line.
TABLE_DEF = re.compile(
    r"^(const\s+(?:uint32_t|X86FuncMapping|X86Symbol)\s+"
    r"(?:" + "|".join(TABLES) + r")\s*(?:\[\])?\s*=)",
    flags=re.MULTILINE,
)

# These are the real SDK x86_ops.h's platform-specific definitions.
HOST_BLOCK = re.compile(
    r"(?m)^#ifdef X86_GAME_CODE\n"
    r".*?"
    r"^#endif\n"
    r"(?=\n#define X86_FUNC\()",
    flags=re.DOTALL | re.MULTILINE,
)
PORTABLE_BLOCK = '''// HulkPocket ELF ABI overlay (upstream header kept unchanged in the SDK).
#define X86_HOST_API __attribute__((visibility("default")))
#define X86_HOST_EXPORT __attribute__((visibility("default")))
// Recompiled fallback is weak so a strong game override can replace it.
// The fallback and weak alias are emitted by the SDK in the same TU.
#define X86_WEAK_FUNC(name, impl) \\
    extern "C" void name(X86Context&, uint8_t*) __attribute__((weak, alias(#impl)));
'''

def rewrite_mapping(source: str) -> str:
    def add_export(m: re.Match[str]) -> str:
        return f'extern "C" {EXPORT} {m.group(1)}'
    rewritten, number = TABLE_DEF.subn(add_export, source)
    if number < 7 or "X86FuncMappings" not in source:
        raise ValueError("unexpected SDK generated mapping format")
    return rewritten

def rewrite_sdk_ops(source: str) -> str:
    updated, number = HOST_BLOCK.subn(lambda _m: PORTABLE_BLOCK, source, count=1)
    if number != 1:
        raise ValueError("upstream x86_ops.h linkage macro block changed")
    return updated

def prepare(generated: pathlib.Path, sdk: pathlib.Path, output: pathlib.Path) -> None:
    mapping = generated / "x86_func_mapping.cpp"
    shared = generated / "x86_recomp_shared.h"
    sdk_ops = sdk / "runtime" / "cpu" / "x86_ops.h"
    sdk_context = sdk / "runtime" / "cpu" / "x86_context.h"
    if not mapping.is_file() or not shared.is_file() or not sdk_ops.is_file() or not sdk_context.is_file():
        raise FileNotFoundError("missing SDK generated or runtime source")
    parts = sorted(generated.glob("x86_recomp.*.cpp"))
    if not parts:
        raise ValueError("no generated recompiled instruction partitions")
    output.mkdir(parents=True, exist_ok=True)
    (output / "cpu").mkdir(exist_ok=True)
    (output / "cpu" / "x86_ops.h").write_text(
        rewrite_sdk_ops(sdk_ops.read_text(encoding="utf-8")), encoding="utf-8"
    )
    shutil.copyfile(sdk_context, output / "cpu" / "x86_context.h")
    (output / "x86_func_mapping.cpp").write_text(
        rewrite_mapping(mapping.read_text(encoding="utf-8")), encoding="utf-8"
    )
    shutil.copyfile(shared, output / shared.name)
    for part in parts:
        shutil.copyfile(part, output / part.name)
    # The actual game-specific symbol config remains untouched.
    for filename in ("image_symbols.h",):
        if (generated / filename).is_file():
            shutil.copyfile(generated / filename, output / filename)

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generated", type=pathlib.Path, required=True)
    parser.add_argument("--sdk", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    prepare(args.generated, args.sdk, args.output)
    print("Prepared XboxXBERecomp C++ for portable ELF linkage:", args.output)

if __name__ == "__main__":
    main()
