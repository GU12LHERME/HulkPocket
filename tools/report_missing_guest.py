#!/usr/bin/env python3
"""Read-only report for missing guest entries. Original files stay local."""
import argparse
import collections
import pathlib
import re
import struct

def sections(data, target):
    def u32(at):
        return struct.unpack_from("<I", data, at)[0]
    if len(data)<0x180 or data[:4]!=b"XBEH":
        raise ValueError("Not an XBE")
    base, count, table=u32(0x104),u32(0x11c),u32(0x120)-u32(0x104)
    if not 0<count<=512 or table<0 or table+count*56>len(data):
        raise ValueError("Invalid section table")
    for i in range(count):
        flags,va,size,raw,n,name=struct.unpack_from("<6I",data,table+i*56)
        if va<=target<va+size:
            name_off=name-base
            section=data[name_off:name_off+32].split(b"\0")[0].decode("ascii","replace") if 0<=name_off<len(data) else "?"
            print(f"XBE section={section} flags={flags:08X} executable={bool(flags&4)} VA={va:08X} size={size:08X}")
            delta=target-va
            if delta<n and raw+n<=len(data):
                print("Target bytes:",data[raw+delta:raw+min(n,delta+32)].hex(" "))
            else:
                print("Target has no file-backed bytes")
            return
    print("Target outside XBE sections")

def source_report(folder,target,caller):
    wanted={target}
    if caller: wanted.add(caller)
    pattern=re.compile(r"^\s*//\s+([0-9A-Fa-f]{8})\s+(.*)")
    function=re.compile(r"X86_FUNC\(([^)]+)\)")
    ref=re.compile(r"\b0x0*"+f"{target:X}"+r"\b",re.I)
    found=set()
    refs=0
    for path in sorted(folder.glob("x86_recomp.*.cpp")):
        name=""
        history=collections.deque(maxlen=5)
        remaining=0
        with path.open(encoding="utf-8",errors="replace") as src:
            for line in src:
                fn=function.search(line)
                if fn:
                    name=fn.group(1);history.clear();remaining=0
                ins=pattern.match(line)
                if not ins: continue
                va=int(ins[1],16)
                item=f"{va:08X} {ins[2]}"
                if remaining:
                    print(item);remaining-=1
                if va in wanted and va not in found:
                    found.add(va)
                    print(f"Source {va:08X}: {name} in {path.name}")
                    print("\n".join([*history,item]))
                    remaining=5
                if refs<8 and ref.search(ins[2]):
                    print(f"Reference {name}: {item}");refs+=1
                history.append(item)
    for va in sorted(wanted-found):
        print(f"No exact generated instruction at {va:08X}")
    mapping=folder/"x86_func_mapping.cpp"
    if mapping.is_file():
        pat=re.compile(r"\b0x0*"+f"{target:X}"+r"\b",re.I)
        print("Mapping entry:", "present" if pat.search(mapping.read_text()) else "absent")

def main():
    import sys
    if sys.argv[1:]==["--self-test"]:
        data=bytearray(0x400);data[:4]=b"XBEH"
        for at,v in [(0x104,0x10000),(0x11c,1),(0x120,0x10200)]:
            struct.pack_into("<I",data,at,v)
        struct.pack_into("<6I",data,0x200,4,0x20000,0x10,0x300,0x10,0x10280)
        data[0x280:0x286]=b".text\0";data[0x300]=0xc3
        sections(data,0x20000)
        return
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("xbe",type=pathlib.Path);p.add_argument("generated",type=pathlib.Path)
    g=p.add_mutually_exclusive_group(required=True)
    g.add_argument("--target",type=lambda s:int(s,16));g.add_argument("--log",type=pathlib.Path)
    a=p.parse_args();caller=0
    target=a.target
    if a.log:
        m=re.search(r"MISSING GUEST target=([0-9A-Fa-f]{8}) caller=([0-9A-Fa-f]{8})",a.log.read_text(errors="replace"))
        if not m: raise SystemExit("Missing guest entry not found in log")
        target,caller=(int(x,16) for x in m.groups())
    print(f"Missing guest target={target:08X} caller={caller:08X}")
    sections(a.xbe.read_bytes(),target)
    source_report(a.generated,target,caller)

if __name__=="__main__":
    main()
