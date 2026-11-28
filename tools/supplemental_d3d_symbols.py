#!/usr/bin/env python3
"""Verify XDK D3D symbols that the generated image does not name.

D3DDevice_SetBackMaterial is called by the title's two-sided material setup
(FUN_00329f70 sets the back material to the front one; documented by the
pinned Thunderclap reference, src/platform/xbox/pure3d/d3dSimpleShader.cpp
and src/platform/sdl/xdk/imports.h: 0x0037DD80) but XboxXBERecomp's symbol
database does not name it. The symbol is emitted only when, locally:

- the image is address-keyed (not IMAGE_NAMED) and lacks the symbol;
- anchor symbols match that build exactly (Direct3D_CreateDevice 0037D900,
  D3DDevice_SetMaterial 0037DD30, D3DDevice_Swap 00381DF0);
- sub_0037DD80 exists in the generated code and its disassembly comments are
  structurally identical to D3DDevice_SetMaterial (only numbers differ).

Writes a header with HAS_/SYM_ defines to a work directory outside the
cached object fingerprint. Prints addresses only, never code.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys
import tempfile

ANCHORS = {"Direct3D_CreateDevice": "0037D900", "D3DDevice_SetMaterial": "0037DD30", "D3DDevice_Swap": "00381DF0"}
CANDIDATES = {"D3DDevice_SetBackMaterial": ("0037DD80", "D3DDevice_SetMaterial")}
FUNC = re.compile(r"X86_FUNC\(sub_([0-9A-Fa-f]{8})\)")
INSN = re.compile(r"^\s*//\s+([0-9A-Fa-f]{8})\s+(.*)$")
NUMBER = re.compile(r"\b(?:0x[0-9A-Fa-f]+|[0-9A-Fa-f]+h|\d+)\b")


def symbols(header: str) -> dict[str, str]:
    found = {}
    for name, addr in re.findall(r"^#define SYM_(\w+) ([0-9A-Fa-f]{8})$", header, re.M):
        if re.search(r"^#define HAS_" + name + r" 1$", header, re.M):
            found[name] = addr.upper()
    return found


def function_shapes(sources: list[pathlib.Path], wanted: set[str]) -> dict[str, list[str]]:
    """Normalized instruction text per wanted function (numbers -> '#')."""
    shapes: dict[str, list[str]] = {}
    for path in sources:
        current = None
        with path.open(encoding="utf-8", errors="replace") as src:
            for line in src:
                m = FUNC.search(line)
                if m:
                    current = m[1].upper() if m[1].upper() in wanted else None
                    if current is not None:
                        shapes.setdefault(current, [])
                    continue
                if current is None:
                    continue
                m = INSN.match(line)
                if m:
                    shapes[current].append(NUMBER.sub("#", m[2].strip().lower()))
    return shapes


def generate(prepared: pathlib.Path, output: pathlib.Path) -> list[str]:
    header = (prepared / "image_symbols.h").read_text(encoding="utf-8", errors="replace")
    lines = ["#pragma once", "// Locally verified supplemental XDK symbols (tools/supplemental_d3d_symbols.py)."]
    accepted: list[str] = []
    named = bool(re.search(r"^#define IMAGE_NAMED 1$", header, re.M))
    known = symbols(header)
    anchors_ok = all(known.get(n) == a for n, a in ANCHORS.items())
    if not named and anchors_ok:
        sources = sorted(prepared.glob("x86_recomp.*.cpp"))
        wanted = {addr for addr, _ in CANDIDATES.values()} | {known[t] for _, t in CANDIDATES.values()}
        shapes = function_shapes(sources, wanted)
        for name, (addr, template) in CANDIDATES.items():
            if name in known:
                continue
            body, reference = shapes.get(addr), shapes.get(known[template])
            if body and reference and body == reference:
                lines += [f"#define HAS_{name} 1", f"#define SYM_{name} {addr}"]
                accepted.append(f"{name}={addr}")
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return accepted


def self_test() -> None:
    template = ["mov eax, dword ptr [esp + 4]", "push esi", "push edi", "mov esi, eax",
                "mov edi, 0x{off}", "mov ecx, 0x11", "rep movsd dword ptr es:[edi], dword ptr [esi]",
                "or dword ptr [0x39db08], 0x{flag}", "pop edi", "pop esi", "ret 4"]
    def func(addr: str, off: str, flag: str) -> str:
        out = [f"X86_FUNC(sub_{addr}) {{"]
        for i, ins in enumerate(template):
            out.append(f"    // {int(addr, 16) + i:08X}  {ins.format(off=off, flag=flag)}")
        return "\n".join(out) + "\n}\n"
    anchors = "".join(f"#define HAS_{n} 1\n#define SYM_{n} {a}\n" for n, a in ANCHORS.items())
    with tempfile.TemporaryDirectory() as tmp:
        d = pathlib.Path(tmp)
        out = d / "extra.h"
        (d / "image_symbols.h").write_text(anchors)
        (d / "x86_recomp.0.cpp").write_text(func("0037DD30", "39e1d4", "1000") + func("0037DD80", "39e218", "2000"))
        assert generate(d, out) == ["D3DDevice_SetBackMaterial=0037DD80"], "accept matching build"
        assert "#define SYM_D3DDevice_SetBackMaterial 0037DD80" in out.read_text()
        # Structurally different body: rejected.
        (d / "x86_recomp.0.cpp").write_text(func("0037DD30", "39e1d4", "1000") +
                                            func("0037DD80", "39e218", "2000").replace("rep movsd", "rep stosd"))
        assert generate(d, out) == [], "reject different body"
        # Missing function: rejected.
        (d / "x86_recomp.0.cpp").write_text(func("0037DD30", "39e1d4", "1000"))
        assert generate(d, out) == [], "reject missing function"
        # Different build (anchor mismatch): rejected.
        (d / "x86_recomp.0.cpp").write_text(func("0037DD30", "39e1d4", "1000") + func("0037DD80", "39e218", "2000"))
        (d / "image_symbols.h").write_text(anchors.replace("00381DF0", "00381E00"))
        assert generate(d, out) == [], "reject other build"
        # Already named by the generator: no duplicate define.
        (d / "image_symbols.h").write_text(anchors + "#define HAS_D3DDevice_SetBackMaterial 1\n#define SYM_D3DDevice_SetBackMaterial 0037DD80\n")
        assert generate(d, out) == [] and "SYM_D3DDevice_SetBackMaterial" not in out.read_text()
        # Named images are never patched.
        (d / "image_symbols.h").write_text("#define IMAGE_NAMED 1\n" + anchors)
        assert generate(d, out) == []
    print("Supplemental D3D symbol tests passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--prepared", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not args.prepared or not args.output:
        parser.error("--prepared and --output are required")
    print("Supplemental D3D symbols:", ", ".join(generate(args.prepared, args.output)) or "none")
    return 0


if __name__ == "__main__":
    sys.exit(main())
