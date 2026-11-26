#!/usr/bin/env python3
"""Only activate the USA fiber replacement after checking the local XDK body."""
import re
import pathlib

def norm(s):
    s=s.lower().replace("dword ptr","")
    s=re.sub(r"0x[0-9a-f]+",lambda m:str(int(m[0],16)),s)
    return re.sub(r"\s+","",s)

def generate_fibers(folder,output):
    functions=re.compile(r"^\s*X86_FUNC\(([^)]+)\)\s*\{",re.M)
    targets={name:[] for name in ("0035551D","0035550A","00355439")}
    for path in sorted(folder.glob("x86_recomp.*.cpp")):
        src=path.read_text(encoding="utf-8",errors="replace")
        for m in functions.finditer(src):
            name=m[1].removeprefix("__imp__sub_")
            if name not in targets:continue
            if not re.search(r"^\s*X86_WEAK_FUNC\(sub_"+name+r",\s*__imp__sub_"+name+r"\)",src,re.M):continue
            end=functions.search(src,m.end())
            chunk=src[m.end():end.start() if end else len(src)]
            ops=[i.strip() for i in re.findall(r"^\s*//\s+[0-9a-fA-F]{8}\s+(.*)$",chunk,re.M)]
            targets[name].append(ops)
    enabled=all(len(x)==1 and x[0] for x in targets.values())
    if enabled:
        ops=[norm(x) for x in targets["0035551D"][0]]
        # XDK 5849 sequence from the upstream Hulk USA fiber adapter:
        # save nonvolatile x86 registers, save ESP in current record +0xC,
        # switch ESP to next record +0xC, store next in TLS +0x10, ret 4.
        required=["pushebp","pushesi","pushedi","pushebx",
                  "mov[ecx+12],esp","movesp,[eax+12]",
                  "mov[edx+16],eax","popebx","popedi","popesi","popebp","ret4"]
        pos=0
        for op in ops:
            if pos<len(required) and op==required[pos]:pos+=1
        enabled=pos==len(required) and any("fs:" in op for op in ops)
        if not enabled:
            print("Fiber signature differs; local original switch instructions:",flush=True)
            for op in targets["0035551D"][0][:48]:print(" FIBER SIGNATURE",op,flush=True)
    output.write_text("#pragma once\n#define HULK_VERIFIED_FIBERS "+str(int(enabled))+"\n")
    return enabled

if __name__=="__main__":
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        p=pathlib.Path(tmp);out=p/"fiber.h"
        assert not generate_fibers(p,out)
        ops=["push ebp","push esi","push edi","push ebx","push dword ptr fs:[0x0]",
             "mov dword ptr [ecx+0x0c], esp","mov esp, [eax+0xc]",
             "mov [edx+0x10], eax","pop dword ptr fs:[0x0]","pop ebx","pop edi","pop esi","pop ebp","ret 0x04"]
        src=p/"x86_recomp.0.cpp"
        def body(name,ins):
            return ("X86_FUNC(__imp__sub_"+name+") {\n"+
                    "".join("// %08X %s\n"%(int(name,16)+i,op) for i,op in enumerate(ins))+
                    "}\nX86_WEAK_FUNC(sub_"+name+", __imp__sub_"+name+")\n")
        src.write_text(body("0035551D",ops)+body("0035550A",["ret 4"])+body("00355439",["push ebp"]))
        assert generate_fibers(p,out)
        src.write_text(src.read_text().replace("[eax+0xc]","[eax+0x10]"))
        assert not generate_fibers(p,out)
        print("Verified fiber discovery tests passed")
