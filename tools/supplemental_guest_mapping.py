#!/usr/bin/env python3
"""Supplement only exact function starts that already have generated bodies."""
import pathlib
import re
import sys

def generate(prepared, output):
    mapping=(prepared/"x86_func_mapping.cpp").read_text()
    def bound(name):
        m=re.search(r"\b"+name+r"\s*=\s*0x([0-9a-fA-F]+)[uU]?\s*;",mapping)
        if not m: raise ValueError("Missing "+name)
        return int(m[1],16)
    lo,hi=bound("X86CodeBase"),bound("X86CodeEnd")
    table=re.search(r"\bX86FuncMappings\s*\[\s*\]\s*=\s*\{(.*?)\};",mapping,re.S)
    if not table: raise ValueError("Missing original mapping table")
    mapped={int(x,16) for x in re.findall(r"\{\s*0x([0-9a-fA-F]+)[uU]?\s*,",table[1])}
    if not mapped: raise ValueError("Empty original mapping")
    bodies=re.compile(r"^\s*X86_FUNC\(__imp__(sub_([0-9A-Fa-f]{8}))\)\s*\{",re.M)
    aliases=re.compile(r"^\s*X86_WEAK_FUNC\((sub_([0-9A-Fa-f]{8})),\s*__imp__\1\)",re.M)
    extra={}
    for part in sorted(prepared.glob("x86_recomp.*.cpp")):
        src=part.read_text()
        defined={m[1] for m in bodies.finditer(src)}
        for m in aliases.finditer(src):
            name,addr=m[1],int(m[2],16)
            if name not in defined or addr in mapped or not lo<=addr<hi: continue
            if addr in extra: raise ValueError("Duplicate supplemental body")
            extra[addr]=name
    # No stubs from x86_func_mapping.cpp, no interior labels, no guessed ABI.
    # The native linker verifies every symbol; original weak aliases are used.
    lines=["#pragma once", '#include "cpu/x86_context.h"']
    for name in extra.values():
        lines.append(f'extern "C" void {name}(X86Context&, uint8_t*);')
    lines.append("static const X86FuncMapping kHulkExtraMapping[]={")
    for addr,name in sorted(extra.items()):
        lines.append(f"    {{0x{addr:08X}u, {name}}},")
    lines+=["    {0,nullptr}", "};",""]
    output.write_text("\n".join(lines))
    return len(extra)

def fixture(folder):
    folder.mkdir(parents=True,exist_ok=True)
    (folder/"x86_func_mapping.cpp").write_text(
        "const uint32_t X86CodeBase = 0x1000u;\n"
        "const uint32_t X86CodeEnd = 0x3000u;\n"
        "const X86FuncMapping X86FuncMappings[] = {\n"
        "{0x1000u, sub_00001000},\n{0, nullptr},\n};\n"
        "X86_FUNC(__imp__sub_00001100) { x86_trap(ctx, 0x1100u, \"absent\"); }\n"
        "X86_WEAK_FUNC(sub_00001100, __imp__sub_00001100)\n")
    (folder/"x86_recomp.0.cpp").write_text(
        "X86_FUNC(__imp__sub_00001000) {\n}\n"
        "X86_WEAK_FUNC(sub_00001000, __imp__sub_00001000)\n"
        "X86_FUNC(__imp__sub_00002000) {\n // 00002001 ret\n}\n"
        "X86_WEAK_FUNC(sub_00002000, __imp__sub_00002000)\n"
        "X86_WEAK_FUNC(sub_00002100, __imp__sub_00002100)\n")
    out=folder/"hulk_extra_mapping.h"
    assert generate(folder,out)==1
    assert "0x00002000u" in out.read_text()
    for addr in ("00001000","00001100","00002001","00002100"):
        assert addr not in out.read_text()

if __name__=="__main__":
    if sys.argv[1:]==["--self-test"]:
        import tempfile
        with tempfile.TemporaryDirectory() as tmp: fixture(pathlib.Path(tmp))
    elif len(sys.argv)==3 and sys.argv[1]=="--fixture":
        fixture(pathlib.Path(sys.argv[2]))
    else:
        raise SystemExit("Use --self-test or --fixture DIRECTORY")
