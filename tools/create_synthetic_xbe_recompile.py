#!/usr/bin/env python3
"""Create a tiny synthetic original-Xbox XBE solely for CI.

The entry code is an original test sequence of IA-32 bytes:
    mov eax, 0x12345678
    ret
No copyrighted game code is present.
"""
from pathlib import Path
import argparse
import struct

BASE=0x00010000
ENTRY=0x00200000
THUNK=0x00200008

def create(directory: Path) -> tuple[Path,Path]:
    directory.mkdir(parents=True,exist_ok=True)
    xbe=bytearray(0x800)
    xbe[:4]=b"XBEH"
    def u32(o,v):
        struct.pack_into("<I",xbe,o,v)
    u32(0x104,BASE)
    u32(0x108,0x400)
    u32(0x10C,0x201000)
    u32(0x118,BASE+0x180)
    u32(0x11C,1)
    u32(0x120,BASE+0x240)
    u32(0x128,ENTRY ^ 0xA8FC57AB)
    u32(0x158,THUNK ^ 0x5B6D40B6)
    u32(0x188,0x7F000001)  # Not a real game.
    u32(0x240,4)  # executable section
    u32(0x244,ENTRY)
    u32(0x248,0x100)
    u32(0x24C,0x600)
    u32(0x250,0x100)
    u32(0x254,BASE+0x340)
    xbe[0x340:0x346]=b".text\x00"
    xbe[0x600:0x606]=bytes.fromhex("B8 78 56 34 12 C3")
    xbe_path=directory/"synthetic.xbe"
    xbe_path.write_bytes(xbe)
    config_path=directory/"synthetic.toml"
    config_path.write_text(
        '[xbe]\n'
        'title = 0x7F000001\n'
        f'entry = 0x{ENTRY:08X}\n'
        'library = "SyntheticRecompiled.dll"\n'
        'out = "generated"\n'
        'partitions = 1\n'
        'code_sections = [".text"]\n'
        '\n[functions]\n'
        f'0x{ENTRY:X} = ""\n',
        encoding="utf-8"
    )
    return xbe_path,config_path

def create_memory_case(directory: Path) -> tuple[Path, Path]:
    """Generate an original IA-32 memory-move/add/store executable test."""
    xbe_path, config_path = create(directory)
    data = bytearray(xbe_path.read_bytes())
    # mov eax,[0x200080]; add eax,7; mov [0x200084],eax; ret
    code = bytes.fromhex(
        "A1 80 00 20 00"
        " 83 C0 07"
        " A3 84 00 20 00"
        " C3"
    )
    data[0x600:0x600 + len(code)] = code
    struct.pack_into("<I", data, 0x680, 0x12345670)
    xbe_path.write_bytes(data)
    return xbe_path, config_path


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output-dir",type=Path,required=True)
    p.add_argument("--profile",choices=("simple","memory"),default="simple")
    args=p.parse_args()
    xbe,cfg=(create_memory_case(args.output_dir)
             if args.profile=="memory" else create(args.output_dir))
    print(f"Generated original test XBE: {xbe}")
    print(f"Generated recompiler config: {cfg}")

if __name__=="__main__":
    main()
