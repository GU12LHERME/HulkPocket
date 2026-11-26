#!/usr/bin/env python3
"""Generate synthetic symbol headers to compile every conditional audio hook.
No original game data required. Addresses are unique fixture values only."""
import argparse
import pathlib
import re
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("output",type=pathlib.Path)
    p.add_argument("--named",action="store_true")
    a=p.parse_args()
    root=pathlib.Path(__file__).resolve().parent.parent
    source=(root/"native/xbe_d3d8_entry_guard.cpp").read_text()
    names=set(re.findall(r"^(?:SOUND_HOOK|SOUND_STOP|STREAM_CREATE)\(([A-Za-z0-9_]+)[,)]",source,re.M))
    names.update(("Direct3D_CreateDevice","DSound_StreamVtable","DSound_StreamVtable2"))
    boundary=(root/"native/xbe_graphics_boundary.h").read_text()
    ids=set(re.findall(r"\b(kSound\w+)\s*=",boundary))
    hooks=set(re.findall(r"^(?:SOUND_HOOK|STREAM_CREATE)\(\w+,(kSound\w+),",source,re.M))
    if hooks-ids:
        raise SystemExit("Undefined audio hook IDs: "+", ".join(sorted(hooks-ids)))
    lines=["#pragma once"]
    if a.named: lines.append("#define IMAGE_NAMED 1")
    for i,name in enumerate(sorted(names)):
        lines.extend((f"#define HAS_{name} 1",f"#define SYM_{name} {0x300000+i*16:08X}"))
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text("\n".join(lines)+"\n")
    print(f"Enabled {len(names)} synthetic symbols, {len(hooks)} audio operation IDs verified")
if __name__=="__main__":main()
