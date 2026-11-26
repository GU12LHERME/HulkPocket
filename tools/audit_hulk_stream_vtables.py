#!/usr/bin/env python3
"""Local XDK 5849 stream vtable audit; reports addresses only, never guesses hooks."""
import argparse
import pathlib
import re
import struct

ANCHORS=("GetInfo","GetStatus","Process","Discontinuity","Flush")

def inspect(data, symbols, mapped):
    def u32(o):
        return struct.unpack_from("<I",data,o)[0]
    if len(data)<0x124 or data[:4]!=b"XBEH":
        raise ValueError("Invalid XBE header")
    base,count,table=u32(0x104),u32(0x11c),u32(0x120)-u32(0x104)
    if not 0<count<=512 or table<0 or table+count*56>len(data):
        raise ValueError("Invalid XBE sections")
    sections=[]
    for i in range(count):
        flags,va,size,raw,n=struct.unpack_from("<5I",data,table+i*56)
        if raw+n>len(data):
            raise ValueError("Invalid XBE section extent")
        sections.append((flags,va,min(size,n),raw))
    def offset(va,n,executable=False):
        for flags,start,size,raw in sections:
            if start<=va and va+n<=start+size and (not executable or flags&4):
                return raw+va-start
        raise ValueError(f"VA not file-backed: {va:08X}")
    vt=symbols.get("DSound_StreamVtable")
    vt2=symbols.get("DSound_StreamVtable2")
    if not vt or not vt2 or vt==vt2:
        raise ValueError("Missing generated stream vtable symbols")
    entries=struct.unpack_from("<7I",data,offset(vt,28))
    for target in entries:
        offset(target,1,True)
    # This candidate order is accepted only if all five independent
    # known generated symbols match the actual primary table entries.
    anchors=tuple(symbols.get("CDirectSoundStream_"+name) for name in ANCHORS)
    matched=entries[2:]==anchors
    return vt,vt2,entries,matched,[x in mapped for x in entries]

def symbols_from(text):
    return {name:int(value,16) for name,value in re.findall(
        r"^#define SYM_(\w+) ([0-9A-Fa-f]{8})$",text,re.M)
        if re.search(r"^#define HAS_"+re.escape(name)+r" 1$",text,re.M)}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("xbe",type=pathlib.Path)
    p.add_argument("generated",type=pathlib.Path)
    a=p.parse_args()
    symbols=symbols_from((a.generated/"image_symbols.h").read_text())
    mapping=(a.generated/"x86_func_mapping.cpp").read_text()
    table=re.search(r"\bX86FuncMappings\s*\[\s*\]\s*=\s*\{(.*?)\};",mapping,re.S)
    if not table:
        raise ValueError("Missing generated mapping")
    mapped={int(x,16) for x in re.findall(r"\{\s*0x([0-9A-Fa-f]+)[uU]?\s*,",table[1])}
    vt,vt2,entries,matched,known=inspect(a.xbe.read_bytes(),symbols,mapped)
    print(f"DSOUND VTABLE AUDIT primary={vt:08X} secondary={vt2:08X} anchors_match={int(matched)}")
    for i,(addr,present) in enumerate(zip(entries,known)):
        label=(("AddRef candidate","Release candidate")+ANCHORS)[i] if matched else "unclassified"
        print(f"DSOUND VTABLE slot={i} target={addr:08X} mapped={int(present)} {label}")
    print("Review candidate method ABI and any this-adjustment thunk before enabling overrides.")

def self_test():
    data=bytearray(0x1000);data[:4]=b"XBEH"
    for off,value in [(0x104,0x10000),(0x11c,1),(0x120,0x10200)]:
        struct.pack_into("<I",data,off,value)
    struct.pack_into("<5I",data,0x200,4,0x20000,0x800,0x400,0x800)
    entries=tuple(0x20100+i*16 for i in range(7))
    struct.pack_into("<7I",data,0x400,*entries)
    symbols={"DSound_StreamVtable":0x20000,"DSound_StreamVtable2":0x20040}
    symbols.update({"CDirectSoundStream_"+n:entries[i+2] for i,n in enumerate(ANCHORS)})
    assert inspect(data,symbols,{entries[0]})[3:]==(True,[True]+[False]*6)
    symbols["CDirectSoundStream_Process"]+=1
    assert not inspect(data,symbols,set())[3]
    struct.pack_into("<I",data,0x400,0x30000)
    try:
        inspect(data,symbols,set())
    except ValueError:
        pass
    else:
        raise AssertionError("invalid target accepted")
    assert symbols_from("#define SYM_Test 00020100\n#define HAS_Test 1\n")=={"Test":0x20100}
    print("DirectSound vtable audit tests passed")

if __name__=="__main__":
    import sys
    if sys.argv[1:]==["--self-test"]:
        self_test()
    else:
        main()
