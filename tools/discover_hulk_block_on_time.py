#!/usr/bin/env python3
"""Verify the locally generated USA GPU wait function before overriding it."""
import pathlib
import re
import tempfile

EXPECTED={
    0x38a2c0:"push esi",
    0x38a2c1:"mov esi, [0x0039dfa8]",
    0x38a2c7:"mov eax, [esi+0x30]",
    0x38a2ca:"mov ecx, [eax]",
    0x38a2cc:"mov eax, [esi+0x2c]",
    0x38a2d4:"mov edi, [esp+0x0c]",
    0x38a2dc:"cmp ecx, edx",
    0x38a3ed:"mov edx, [esi+0x30]",
    0x38a3f7:"mov ecx, [edx]",
    0x38a405:"ret 0x08",
    0x38a427:"ret 0x08",
}
def generate_block_on_time(folder,output):
    body=re.compile(r"^\s*X86_FUNC\(__imp__sub_0038A2C0\)\s*\{",re.M)
    alias=re.compile(r"^\s*X86_WEAK_FUNC\(sub_0038A2C0,\s*__imp__sub_0038A2C0\)",re.M)
    matches=0
    for path in sorted(folder.glob("x86_recomp.*.cpp")):
        src=path.read_text(encoding="utf-8",errors="replace")
        for m in body.finditer(src):
            end=re.search(r"^\s*X86_FUNC\(",src[m.end():],re.M)
            chunk=src[m.end():m.end()+end.start()] if end else src[m.end():]
            if not alias.search(src):continue
            actual={int(a,16):i.strip().lower() for a,i in re.findall(r"^\s*//\s+([0-9a-fA-F]{8})\s+(.*)$",chunk,re.M)}
            if all(actual.get(a)==i for a,i in EXPECTED.items()):matches+=1
    enabled=matches==1
    output.write_text(f"#pragma once\n#define HULK_VERIFIED_BLOCK_ON_TIME {int(enabled)}\n")
    return enabled

if __name__=="__main__":
    with tempfile.TemporaryDirectory() as tmp:
        p=pathlib.Path(tmp);out=p/"wait.h";src=p/"x86_recomp.0.cpp"
        assert not generate_block_on_time(p,out)
        src.write_text("X86_FUNC(__imp__sub_0038A2C0) {\n"+
            "".join(f"// {a:08X} {i}\n" for a,i in EXPECTED.items())+
            "}\nX86_WEAK_FUNC(sub_0038A2C0, __imp__sub_0038A2C0)\n")
        assert generate_block_on_time(p,out)
        src.write_text(src.read_text().replace("ret 0x08","ret 0x04"))
        assert not generate_block_on_time(p,out)
        print("Verified GPU wait discovery tests passed")
