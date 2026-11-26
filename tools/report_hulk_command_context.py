#!/usr/bin/env python3
"""Read-only, bounded local XDK report for the RP6 0038A2C0 fault."""
import argparse
import collections
import pathlib
import re
import tempfile

FUNC=re.compile(r"^\s*X86_FUNC\(([^)]+)\)")
INS=re.compile(r"^\s*//\s+([0-9a-fA-F]{8})\s+(.*)")
FIELD=re.compile(r"\b(?:0x0*(?:1fa8|3a0058)|(?:1fa8|3a0058)h)\b",re.I)
CALL=re.compile(r"\b(?:call|jmp)\s+(?:0x0*38a2c0|0*38a2c0h|(?:__imp__)?sub_0038a2c0)\b",re.I)
TARGET=re.compile(r"(?:__imp__)?sub_0038a2c0$",re.I)
NEAR=re.compile(r"(?:__imp__)?sub_0031b190$",re.I)

def collect(folder):
    files=sorted(folder.glob("x86_recomp.*.cpp"))
    if not files:raise ValueError(f"No generated sources in {folder}")
    bodies=collections.defaultdict(dict)
    refs=[]
    callers=[]
    pending=[]
    history=collections.deque(maxlen=8)
    for path in files:
        fn=""
        history.clear();pending.clear()
        with path.open(encoding="utf-8",errors="replace") as src:
            for line in src:
                m=FUNC.match(line)
                if m:
                    fn=m[1];history.clear();pending.clear()
                    continue
                m=INS.match(line)
                if not m:continue
                va=int(m[1],16);ins=m[2]
                row=(va,ins)
                for entry in pending:entry[4].append(row)
                pending=[entry for entry in pending if len(entry[4])<8]
                if (TARGET.search(fn) or NEAR.search(fn)) and len(bodies[fn])<220:
                    bodies[fn][va]=ins
                for pattern,dest,cap in ((FIELD,refs,60),(CALL,callers,36)):
                    if len(dest)<cap and pattern.search(ins):
                        entry=[fn,path.name,list(history),row,[]]
                        dest.append(entry);pending.append(entry)
                history.append(row)
    return bodies,refs,callers,len(files)

def report(folder):
    bodies,refs,callers,count=collect(folder)
    print(f"RP6 command-context report; generated files scanned={count}")
    print("FAULT 0038A2C0: ARM64 load through null at guest address 0; source mapping required")
    for fn,body in bodies.items():
        print(f"BODY {fn}: {len(body)} instructions (max 220)")
        for va,ins in sorted(body.items()):print(f"  {va:08X} {ins}")
    if not any(TARGET.search(fn) for fn in bodies):
        print("FAULT BODY NOT FOUND")
    for label,entries in (("CDEVICE+1FA8 REFERENCES (candidate register bases)",refs),("DIRECT CALL/JUMP REFERENCES",callers)):
        print(f"{label}: {len(entries)}")
        for fn,file,before,(va,ins),after in entries:
            print(f"{fn} ({file})")
            for a,t in before:print(f"  {a:08X} {t}")
            print(f"> {va:08X} {ins}")
            for a,t in after:print(f"  {a:08X} {t}")
    print("No runtime execution or modifications; register-relative references are not proof of structure layout.")

def self_test():
    with tempfile.TemporaryDirectory() as tmp:
        folder=pathlib.Path(tmp)
        (folder/"x86_recomp.0.cpp").write_text(
            "X86_FUNC(__imp__sub_0038A2C0) {\n"
            "// 0038A2C0 mov eax, [ecx+0x1fa8]\n"
            "// 0038A2C6 mov edx, [eax+0x30]\n"
            "// 0038A2C9 ret\n"
            "}\nX86_FUNC(__imp__sub_0031B190) {\n"
            "// 0031B190 call 0x38a2c0\n// 0031B195 ret\n}\n")
        bodies,refs,callers,count=collect(folder)
        assert count==1 and len(refs)==len(callers)==1
        assert len(bodies["__imp__sub_0038A2C0"])==3
        assert refs[0][4][0][0]==0x38a2c6
        assert callers[0][4][0][0]==0x31b195
        print("Command-context report self-test passed")

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("generated",nargs="?",type=pathlib.Path)
    parser.add_argument("--self-test",action="store_true")
    args=parser.parse_args()
    if args.self_test:self_test()
    elif args.generated:report(args.generated)
    else:parser.error("Specify generated directory or --self-test")
