#!/usr/bin/env python3
"""Audit undefined imports in Android XboxXBERecomp object files.

Input is only ELF .o objects compiled LOCALLY from a user-owned XBE.
Writes symbol names and occurrence counts, never program bytes or assets.
This is an inventory, not a promise that every import is implemented.
"""
from __future__ import annotations

import argparse
import collections
import json
from pathlib import Path
import subprocess

from ndk_tools import tool

def parse_undefined(output: str) -> list[str]:
    result: list[str] = []
    for raw in output.splitlines():
        line = raw.strip()
        # llvm-nm -u --demangle prints "U symbol..." or "filename: U ..."
        if line.startswith("U "):
            result.append(line[2:].strip())
        elif " U " in line:
            result.append(line.split(" U ", 1)[1].strip())
    return [value for value in result if value]

def classify(name: str) -> str:
    if "x86_icall(" in name or "x86_resume(" in name:
        return "guest_dispatch"
    if name.startswith(("x86_", "X86")):
        return "x86_runtime"
    if any(mark in name for mark in ("kernel::", "mem::", "X86Context", "Hulk", "hulk::")):
        return "guest_kernel_or_hook"
    return "cxx_or_platform"

def audit(nm: Path, objects: list[Path]) -> dict:
    if not nm.is_file() or not objects:
        raise ValueError("missing llvm-nm or compiled object files")
    counts: collections.Counter[str] = collections.Counter()
    defined: set[str] = set()
    for obj in objects:
        if not obj.is_file() or obj.suffix != ".o":
            raise ValueError("audit accepts only existing .o files")
        text = subprocess.check_output(
            [str(nm), "-u", str(obj)], text=True
        )
        counts.update(parse_undefined(text))
        definitions = subprocess.check_output([str(nm), "--defined-only", "--extern-only", "--format=posix", str(obj)], text=True)
        defined.update(line.split()[0] for line in definitions.splitlines() if len(line.split()) >= 2)
    remaining = {name: count for name, count in counts.items() if name not in defined}
    demangled = subprocess.run([str(tool(nm.parent, "llvm-cxxfilt"))], input="\n".join(remaining)+"\n", text=True, capture_output=True, check=True).stdout.splitlines() if remaining else []
    if len(demangled) != len(remaining):
        raise RuntimeError("unexpected demangler output")
    return {
        "object_count": len(objects),
        "unique_undefined": len(remaining),
        "unique_undefined_before_resolution": len(counts),
        "resolved_between_objects": len(counts) - len(remaining),
        "symbols": [
            {"name": display, "mangled": name, "category": classify(display), "references": remaining[name]}
            for name, display in zip(remaining, demangled)
        ],
        "note": "External definitions subtracted by exact mangled name; final strict link remains authoritative."
    }

def main() -> None:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--nm", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("objects", nargs="+", type=Path)
    a=p.parse_args()
    report=audit(a.nm,a.objects)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n",encoding="utf-8")
    print(f"Guest import report: {report['unique_undefined']} unique unresolved references from {report['object_count']} objects")

if __name__=="__main__":
    main()
