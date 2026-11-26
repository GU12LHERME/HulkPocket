#!/usr/bin/env python3
"""Read-only analysis of a repeated original startup frame; no recompile."""
import argparse
import collections
import pathlib
import re
import tempfile

FUNC=re.compile(r"^\s*X86_FUNC\(([^)]+)\)")
INS=re.compile(r"^\s*//\s+([0-9a-fA-F]{8})\s+(.*)")
DIRECT=re.compile(r"^(?:call|jmp)\s+0x([0-9a-f]+)\b",re.I)
ANCHORS={0x31b0ce,0x31b190,0x31b1d4,0x323ea6,0x321843,0x31c10f,0x1bb50e}
def functions(folder):
    for path in sorted(folder.glob("x86_recomp.*.cpp")):
        fn=None; rows=[]
        with path.open(encoding="utf-8",errors="replace") as src:
            for line in src:
                m=FUNC.match(line)
                if m:
                    if fn and rows:yield fn,rows
                    fn=m[1];rows=[]
                elif fn:
                    m=INS.match(line)
                    if m:rows.append((int(m[1],16),m[2].strip()))
        if fn and rows:yield fn,rows

def scan(folder):
    owners={}; incoming=collections.defaultdict(list); names={}; sizes={}
    for fn,rows in functions(folder):
        start=rows[0][0];names[start]=fn;sizes[start]=len(rows)
        for i,(va,ins) in enumerate(rows):
            if va in ANCHORS:
                owners[va]=(start,fn,rows[max(0,i-12):i+13])
            m=DIRECT.match(ins)
            if m:
                dest=int(m[1],16)
                if len(incoming[dest])<24:
                    incoming[dest].append((start,va,ins))
    return owners,incoming,names,sizes

def report(folder,log):
    owners,incoming,names,sizes=scan(folder)
    if not names:raise ValueError("No generated instruction bodies found")
    print(f"Generated functions indexed: {len(names)}")
    seeds=set()
    for va in sorted(ANCHORS):
        if va not in owners:
            print(f"ANCHOR {va:08X} not found");continue
        start,fn,rows=owners[va];seeds.add(start)
        print(f"ANCHOR {va:08X} owner={fn} entry={start:08X} instructions={sizes[start]}")
        for addr,ins in rows:print(f"{'>' if addr==va else ' '} {addr:08X} {ins}")
    # Direct references only. Indirect calls may not appear in this graph.
    seen=set();frontier=seeds;selected=set(seeds)
    for depth in range(3):
        next_set=set()
        for dest in sorted(frontier):
            if dest in seen:continue
            seen.add(dest)
            refs=[r for r in incoming[dest] if r[0]!=dest]
            print(f"CALLERS depth={depth+1} target={dest:08X} {names.get(dest,'?')} refs={len(refs)} (max 24)")
            for source,va,ins in refs:
                print(f"  {names.get(source,'?')} entry={source:08X} at={va:08X} {ins}")
                if source not in seen and len(selected)<36:
                    next_set.add(source);selected.add(source)
        frontier=next_set
    # Print bounded bodies of the identified ancestors and anchor owners.
    for fn,rows in functions(folder):
        start=rows[0][0]
        if start not in selected:continue
        print(f"BODY {fn} {start:08X} instructions={len(rows)} (max 160)")
        if len(rows)<=160:sample=rows
        else:
            # Keep entry and exit plus branch/call contexts, not arbitrary data.
            indices=set(range(24))|set(range(len(rows)-16,len(rows)))
            for i,(va,ins) in enumerate(rows):
                if va in ANCHORS or (DIRECT.match(ins) and int(DIRECT.match(ins)[1],16) in selected):
                    indices.update(range(max(0,i-4),min(len(rows),i+5)))
            sample=[rows[i] for i in sorted(indices)[:160]]
        for va,ins in sample:print(f"  {va:08X} {ins}")
    if log:
        lines=log.read_text(errors="replace").splitlines()
        checks=[x for x in lines if x.startswith("D3D FRAME CHECKSUM ")]
        hashes={m.group(1) for x in checks if (m:=re.search(r"hash=([0-9A-Fa-f]+)",x))}
        print(f"LOG frames={len(checks)} unique_hashes={len(hashes)}")
        for x in lines:
            if x.startswith(("XAPI device initialization:","XLaunchNewImageA request:","FILE OPEN","FILE READ","Guest entry status:")):print(x)
        print("Last presentation:",next((x for x in reversed(lines) if x.startswith("D3D ORIGINAL SWAP OFFSCREEN PASS")),"not logged"))
    print("Disc root environment:", "selected" if __import__("os").environ.get("HULK_DISC_ROOT") else "not selected in this shell")
    print("No game execution, patch, or asset upload. Indirect call targets and branch conditions require further evidence.")

def self_test():
    with tempfile.TemporaryDirectory() as tmp:
        folder=pathlib.Path(tmp)
        (folder/"x86_recomp.0.cpp").write_text(
            "X86_FUNC(__imp__sub_0031B190) {\n// 0031B190 push esi\n// 0031B1D4 ret\n}\n"
            "X86_FUNC(__imp__sub_00100000) {\n// 00100000 call 0x0031B190\n// 00100005 ret\n}\n")
        owners,refs,names,sizes=scan(folder)
        assert owners[0x31b1d4][0]==0x31b190
        assert refs[0x31b190]==[(0x100000,0x100000,"call 0x0031B190")]
        assert len(names)==2 and sizes[0x31b190]==2
        print("Startup loop report self-test passed")

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("generated",nargs="?",type=pathlib.Path)
    p.add_argument("--log",type=pathlib.Path)
    p.add_argument("--self-test",action="store_true")
    a=p.parse_args()
    if a.self_test:self_test()
    elif a.generated:report(a.generated,a.log)
    else:p.error("Specify generated directory or --self-test")
