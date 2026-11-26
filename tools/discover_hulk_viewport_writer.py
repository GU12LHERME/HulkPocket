#!/usr/bin/env python3
"""Enable a pass-through writer observer only on locally verified USA code."""
import re
import pathlib

def generate_writer(folder,output):
    body=re.compile(r"^\s*X86_FUNC\(__imp__sub_0037F720\)\s*\{",re.M)
    alias=re.compile(r"^\s*X86_WEAK_FUNC\(sub_0037F720,\s*__imp__sub_0037F720\)",re.M)
    # Exact instruction comments from the RP6 report. Require all six
    # stores inside the same generated body, not a coincidental address.
    expected={0x37f8d0:"mov [esi+0xee0], eax",0x37f8de:"mov [esi+0xeec], edi",
              0x37f8e4:"mov [esi+0xee4], ecx",0x37f8ea:"mov [esi+0xee8], ebp",
              0x37f8f3:"mov [esi+0xef0], ecx",0x37f8fc:"mov [esi+0xef4], edx"}
    matches=0
    for path in sorted(folder.glob("x86_recomp.*.cpp")):
        src=path.read_text(encoding="utf-8",errors="replace")
        for m in body.finditer(src):
            end=re.search(r"^\s*X86_FUNC\(",src[m.end():],re.M)
            chunk=src[m.end():m.end()+end.start()] if end else src[m.end():]
            if not alias.search(src):continue
            actual={int(a,16):i.strip().lower() for a,i in re.findall(r"^\s*//\s+([0-9a-fA-F]{8})\s+(.*)$",chunk,re.M)}
            if all(actual.get(a)==i for a,i in expected.items()):matches+=1
    enabled=matches==1
    output.write_text(f"#pragma once\n#define HULK_VERIFIED_VIEWPORT_WRITER {int(enabled)}\n")
    return enabled

if __name__=="__main__":
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        p=pathlib.Path(tmp)
        out=p/"writer.h"
        assert not generate_writer(p,out)
        src=p/"x86_recomp.0.cpp"
        src.write_text("X86_FUNC(__imp__sub_0037F720) {\n"
            "// 0037F8D0 mov [esi+0xEE0], eax\n// 0037F8DE mov [esi+0xEEC], edi\n"
            "// 0037F8E4 mov [esi+0xEE4], ecx\n// 0037F8EA mov [esi+0xEE8], ebp\n"
            "// 0037F8F3 mov [esi+0xEF0], ecx\n// 0037F8FC mov [esi+0xEF4], edx\n"
            "}\nX86_WEAK_FUNC(sub_0037F720, __imp__sub_0037F720)\n")
        assert generate_writer(p,out)
        src.write_text(src.read_text().replace("0xEE8","0xEE9"))
        assert not generate_writer(p,out)
        print("Viewport writer discovery tests passed")
