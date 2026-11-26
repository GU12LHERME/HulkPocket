#!/usr/bin/env python3
"""Build locally generated XboxXBERecomp C++ into an Android ARM64 ELF.

This intentionally does not accept an XBE/ISO: users generate the C++
privately with the original SDK and pass its generated/ folder.
No copyrighted Xbox image is uploaded to CI or this repository.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import tempfile

from prepare_xbe_elf import prepare
from audit_xbe_elf import audit
import json

VALID_PART = re.compile(r"x86_recomp\.[0-9]+\.cpp\Z")

def generated_sources(directory: pathlib.Path) -> list[pathlib.Path]:
    parts = sorted(p for p in directory.glob("x86_recomp.*.cpp")
                   if VALID_PART.fullmatch(p.name))
    if not parts or not (directory / "x86_func_mapping.cpp").is_file():
        raise ValueError("missing generated SDK function mapping or instruction partitions")
    return [directory / "x86_func_mapping.cpp", *parts]

def build(
    sdk: pathlib.Path, generated: pathlib.Path, ndk_bin: pathlib.Path,
    output: pathlib.Path, audit_output: pathlib.Path | None = None,
    termux: bool = False, audit_only: bool = False,
) -> pathlib.Path:
    compiler = ndk_bin / ("clang++" if termux else "aarch64-linux-android29-clang++")
    if termux:
        target = subprocess.check_output([str(compiler), "-dumpmachine"], text=True).strip()
        if not target.startswith("aarch64") or "android" not in target:
            raise ValueError("Termux compiler must target Android AArch64")
    if audit_only and audit_output is None:
        raise ValueError("audit-only requires --audit-output")
    nm = ndk_bin / "llvm-nm"
    readelf = ndk_bin / "llvm-readelf"
    for tool in (compiler, nm, readelf):
        if not tool.is_file():
            raise FileNotFoundError(f"Android NDK tool missing: {tool}")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="hulkpocket-xbe-elf-") as temporary:
        work = pathlib.Path(temporary)
        source_dir = work / "prepared"
        object_dir = work / "objects"
        object_dir.mkdir()
        prepare(generated, sdk, source_dir)
        objects: list[pathlib.Path] = []
        for source in generated_sources(source_dir):
            obj = object_dir / (source.stem + ".o")
            command = [
                str(compiler), "-std=c++20", "-O2", "-fPIC",
                "-fno-strict-aliasing", "-Wall", "-Wextra",
                "-Wno-unused-parameter", "-Wno-unused-variable",
                "-I", str(source_dir), "-c", str(source), "-o", str(obj),
            ]
            subprocess.run(command, check=True)
            objects.append(obj)
        if audit_output is not None:
            report = audit(nm, objects)
            audit_output.parent.mkdir(parents=True, exist_ok=True)
            audit_output.write_text(
                json.dumps(report, indent=2, ensure_ascii=False) + "\n",
                encoding="utf-8",
            )
        if audit_only:
            return audit_output
        # The generated code often imports SDK services such as
        # x86_icall, kernel accessors and game hooks. Production builds
        # must explicitly resolve all of these against the host runtime.
        # Do not silently emit a module with missing dynamic symbols.
        subprocess.run([
            str(compiler), "-shared", "-Wl,-z,defs",
            "-Wl,-z,max-page-size=16384",
            *map(str, objects), "-o", str(output),
        ], check=True)
        symbols = subprocess.check_output([str(nm), "-D", str(output)], text=True)
        for name in ("X86FuncMappings", "X86CodeBase", "X86CodeEnd", "X86EntryPoint"):
            if not re.search(r"\b" + name + r"\s*$", symbols, re.MULTILINE):
                raise RuntimeError(f"missing exported SDK symbol: {name}")
        header = subprocess.check_output([str(readelf), "-h", str(output)], text=True)
        if "AArch64" not in header or "ELF64" not in header:
            raise RuntimeError("generated shared object is not Android AArch64 ELF64")
    return output

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", required=True, type=pathlib.Path)
    parser.add_argument("--generated", required=True, type=pathlib.Path)
    parser.add_argument("--ndk-bin", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--audit-output", type=pathlib.Path, default=None)
    parser.add_argument("--termux", action="store_true", help="Use native Android clang++ in --ndk-bin")
    parser.add_argument("--audit-only", action="store_true", help="Compile and inventory imports without linking")
    args = parser.parse_args()
    result = build(args.sdk, args.generated, args.ndk_bin, args.output, args.audit_output, args.termux, args.audit_only)
    print(f"{'Private import audit' if args.audit_only else 'Validated Android ARM64 ELF'}: {result}")

if __name__ == "__main__":
    main()
