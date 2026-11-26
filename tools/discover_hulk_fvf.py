#!/usr/bin/env python3
"""Recover the original USA XDK FVF record pointer from local instructions."""
import collections
import pathlib
import re
import struct
import sys
from recompile_hulk_local import inspect_xbe
DEVICE=0x39dfb0
OFFSET=0x794
FIELD=DEVICE+OFFSET

def discover(data,records):
    if len(data)<0x180 or data[:4]!=b"XBEH":raise ValueError("Not XBE")
    base=struct.unpack_from("<I",data,0x104)[0]
    count,table=struct.unpack_from("<II",data,0x11c)
    table-=base
    if not 0<count<=512 or table<0 or table+56*count>len(data):raise ValueError("Invalid sections")
    sections=[]
    for i in range(count):
        flags,va,size,raw,n=struct.unpack_from("<5I",data,table+56*i)
        if raw+n>len(data):raise ValueError("Invalid raw section")
        sections.append((flags,va,size,raw,min(size,n)))
    def read(va,n):
        for flags,start,size,raw,length in sections:
            if flags&4 and start<=va and va+n<=start+length:
                o=raw+va-start
                return data[o:o+n]
        return b""
    def pointer(p):
        return p%4==0 and p>=0x10000 and not DEVICE<=p<DEVICE+0x24a0 and any(
            flags&1 and va<=p and p+8<=va+size for flags,va,size,raw,n in sections)
    candidates=set()
    history=collections.deque(maxlen=6)
    refs=[]
    previous=None
    for fn,va,ins in records:
        if previous is None or previous[0]!=fn:history.clear()
        code=read(va,10)
        p=None
        if len(code)==10:
            disp=struct.unpack_from("<I",code,2)[0]
            if code[:2]==b"\xc7\x05" and disp==FIELD:
                p=struct.unpack_from("<I",code,6)[0]
            elif code[0]==0xc7 and code[1] in (0x80,0x81,0x82,0x83,0x85,0x86,0x87) and disp==OFFSET:
                p=struct.unpack_from("<I",code,6)[0]
            elif previous and previous[0]==fn and previous[1]+5==va:
                prev=read(previous[1],5)
                if len(prev)==5 and 0xb8<=prev[0]<=0xbf:
                    reg=prev[0]-0xb8
                    m=code[1] if code[0]==0x89 else -1
                    if m>=0 and ((m>>3)&7)==reg and ((m&0xc7)==5 and disp==FIELD or
                            (m&0xc0)==0x80 and (m&7)!=4 and disp==OFFSET):
                        p=struct.unpack_from("<I",prev,1)[0]
                    elif code[0]==0xa3 and reg==0 and struct.unpack_from("<I",code,1)[0]==FIELD:
                        p=struct.unpack_from("<I",prev,1)[0]
        if p is not None and pointer(p):
            candidates.add(p)
            print(f"FVF candidate {p:08X} via {fn} {va:08X}: {ins}",file=sys.stderr)
        if re.search(r"\b(?:0x0*794|0x0*39e744|794h|39e744h)\b",ins,re.I) or p is not None:
            if len(refs)<48:refs.append((fn,list(history),va,ins))
        history.append((va,ins));previous=(fn,va)
    if len(candidates)!=1:
        print("FVF discovery inconclusive; original instruction context:",file=sys.stderr)
        for fn,prev,va,ins in refs:
            print(fn,file=sys.stderr)
            for a,t in prev:print(f"  {a:08X} {t}",file=sys.stderr)
            print(f"> {va:08X} {ins}",file=sys.stderr)
        return None
    return next(iter(candidates))

def records(folder):
    fnpat=re.compile(r"X86_FUNC\(([^)]+)\)")
    ipat=re.compile(r"^\s*//\s+([0-9a-fA-F]{8})\s+(.*)")
    for path in sorted(folder.glob("x86_recomp.*.cpp")):
        fn=""
        with path.open(encoding="utf-8",errors="replace") as src:
            for line in src:
                m=fnpat.search(line)
                if m:fn=m[1]
                m=ipat.match(line)
                if m:
                    va=int(m[1],16)
                    if 0x37d900<=va<0x38b000:yield fn,va,m[2]

def self_test():
    data=bytearray(0x1000);data[:4]=b"XBEH"
    for at,v in [(0x104,0x10000),(0x11c,2),(0x120,0x10200)]:
        struct.pack_into("<I",data,at,v)
    struct.pack_into("<5I",data,0x200,4,0x37d900,0x100,0x400,0x100)
    struct.pack_into("<5I",data,0x238,1,0x390000,0x1000,0x500,0x100)
    data[0x400:0x40a]=b"\xc7\x05"+struct.pack("<II",FIELD,0x390040)
    assert discover(data,[("create",0x37d900,"mov [0x39e744], 0x390040")])==0x390040
    data[0x410:0x41a]=b"\xc7\x83"+struct.pack("<II",OFFSET,0x390080)
    assert discover(data,[("create",0x37d900,"mov"),("create",0x37d910,"mov")]) is None
    print("FVF discovery tests passed")

if __name__=="__main__":
    if sys.argv[1:]==["--self-test"]:self_test()
    else:
        xbe,folder=map(pathlib.Path,sys.argv[1:])
        if inspect_xbe(xbe)[2]!="usa":raise ValueError("FVF resolver USA only")
        result=discover(xbe.read_bytes(),records(folder))
        if result:print(f"{result:08X}")
