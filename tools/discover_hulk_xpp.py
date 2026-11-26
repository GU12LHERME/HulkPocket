#!/usr/bin/env python3
"""Inspect the observed USA XPP initializer descriptor table; never guess code."""
import argparse
import pathlib
import re
import struct
from recompile_hulk_local import inspect_xbe

def discover(data, mapped):
    def u32(o):return struct.unpack_from("<I",data,o)[0]
    base,count,table=u32(0x104),u32(0x11c),u32(0x120)-u32(0x104)
    if not 0<count<=512 or table<0 or table+count*56>len(data):raise ValueError("Invalid sections")
    sections=[]
    for i in range(count):
        flags,va,size,raw,n=struct.unpack_from("<5I",data,table+i*56)
        if raw+n>len(data):raise ValueError("Invalid raw section")
        sections.append((flags,va,min(size,n),raw))
    def read(va,n,executable=False):
        for flags,start,size,raw in sections:
            if start<=va and va+n<=start+size and (not executable or flags&4):
                off=raw+va-start
                return data[off:off+n]
        raise ValueError(f"VA outside file-backed section: {va:08X}")
    seeds=set()
    # Actual guest trace: EDI=003EA754, ESI=003EA764,
    # caller 003EAC68 call [eax+4], return 003EAC6B.
    if read(0x3eac68,3,True)!=b"\xff\x50\x04":raise ValueError("Unexpected XPP caller")
    for slot in range(0x3ea754,0x3ea764,4):
        descriptor=struct.unpack("<I",read(slot,4))[0]
        if not descriptor:continue
        target=struct.unpack("<I",read(descriptor+4,4))[0]
        if not target or target in mapped:continue
        code=read(target,3,True)
        if code!=b"\x55\x8b\xec":
            print(f"Manual review required: {target:08X}",file=__import__("sys").stderr)
            continue
        seeds.add(target)
    return sorted(seeds)

def self_test():
    data=bytearray(0x1000)
    for off,value in [(0x104,0x10000),(0x11c,1),(0x120,0x10200)]:
        struct.pack_into("<I",data,off,value)
    struct.pack_into("<5I",data,0x200,4,0x3ea700,0x900,0x400,0x900)
    def write(va,b):off=0x400+va-0x3ea700;data[off:off+len(b)]=b
    write(0x3eac68,b"\xff\x50\x04")
    write(0x3ea754,struct.pack("<4I",0x3ea830,0x3ea840,0,0))
    write(0x3ea834,struct.pack("<I",0x3ea91c))
    write(0x3ea844,struct.pack("<I",0x3ea950))
    write(0x3ea91c,b"\x55\x8b\xec");write(0x3ea950,b"\x55\x8b\xec")
    assert discover(data,{0x3ea950})==[0x3ea91c]
    assert not discover(data,{0x3ea950,0x3ea91c})
    print("XPP table discovery tests passed")

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("xbe",type=pathlib.Path)
    p.add_argument("generated",type=pathlib.Path)
    a=p.parse_args()
    if inspect_xbe(a.xbe)[2]!="usa":raise ValueError("XPP table address verified only for USA")
    mapping=(a.generated/"x86_func_mapping.cpp").read_text()
    table=re.search(r"\bX86FuncMappings\s*\[\s*\]\s*=\s*\{(.*?)\};",mapping,re.S)
    if not table:raise ValueError("Missing mapping table")
    mapped={int(x,16) for x in re.findall(r"\{\s*0x([0-9a-fA-F]+)[uU]?\s*,",table[1])}
    for target in discover(a.xbe.read_bytes(),mapped):print(f"{target:08X}")

if __name__=="__main__":
    import sys
    if sys.argv[1:]==["--self-test"]:self_test()
    else:main()
