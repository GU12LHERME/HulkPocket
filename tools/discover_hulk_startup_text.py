#!/usr/bin/env python3
"""Opt in to a pass-through observer of the verified startup text call."""
import pathlib
import re

def generate_startup_text(folder,output):
    fn=re.compile(r"^\s*X86_FUNC\(([^)]+)\)\s*\{",re.M)
    alias=re.compile(r"^\s*X86_WEAK_FUNC\(sub_0029B330,\s*__imp__sub_0029B330\)",re.M)
    expected={0x1bb32f:"push 0x00",0x1bb331:"lea eax, [esp+0x528]",
              0x1bb338:"push eax",0x1bb339:"call 0x0029b330",
              0x1bb33e:"mov edx, [edi]"}
    callers=0;targets=0
    for path in sorted(folder.glob("x86_recomp.*.cpp")):
        src=path.read_text(encoding="utf-8",errors="replace")
        for m in fn.finditer(src):
            if m[1]=="__imp__sub_0029B330" and alias.search(src):targets+=1
            if m[1]!="__imp__sub_001BB2ED":continue
            end=fn.search(src,m.end())
            chunk=src[m.end():end.start() if end else len(src)]
            actual={int(a,16):i.strip().lower() for a,i in re.findall(r"^\s*//\s+([0-9a-fA-F]{8})\s+(.*)$",chunk,re.M)}
            if all(actual.get(a)==i for a,i in expected.items()):callers+=1
    enabled=callers==1 and targets==1
    output.write_text(f"#pragma once\n#define HULK_VERIFIED_STARTUP_TEXT {int(enabled)}\n")
    return enabled

if __name__=="__main__":
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        p=pathlib.Path(tmp);out=p/"observer.h"
        assert not generate_startup_text(p,out)
        src=p/"x86_recomp.0.cpp"
        src.write_text("X86_FUNC(__imp__sub_001BB2ED) {\n"
                       "// 001BB32F push 0x00\n// 001BB331 lea eax, [esp+0x528]\n"
                       "// 001BB338 push eax\n// 001BB339 call 0x0029B330\n"
                       "// 001BB33E mov edx, [edi]\n}\n"
                       "X86_FUNC(__imp__sub_0029B330) {\n}\n"
                       "X86_WEAK_FUNC(sub_0029B330, __imp__sub_0029B330)\n")
        assert generate_startup_text(p,out)
        src.write_text(src.read_text().replace("0x528","0x524"))
        assert not generate_startup_text(p,out)
        print("Startup text observer discovery tests passed")
