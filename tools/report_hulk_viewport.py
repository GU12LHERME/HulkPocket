#!/usr/bin/env python3
"""Report local generated instruction candidates; no address patching."""
import collections
import pathlib
import re
import sys
from discover_hulk_fvf import records

def main():
    # CDevice USA 0039DFB0 + viewport fields EE0..EF4. Register-relative
    # references are candidates only; their base register is not proven.
    pattern=re.compile(r"\b(?:0x0*(?:ee0|ee4|ee8|eec|ef0|ef4|39ee90|39ee94|39ee98|39ee9c|39eea0|39eea4)|(?:ee0|ee4|ee8|eec|ef0|ef4|39ee90|39ee94|39ee98|39ee9c|39eea0|39eea4)h)\b",re.I)
    hist=collections.deque(maxlen=5)
    previous=None
    count=0
    print("D3D VIEWPORT LOCAL INSTRUCTION CANDIDATES (unverified register bases)")
    for fn,va,ins in records(pathlib.Path(sys.argv[1])):
        if fn!=previous:hist.clear()
        if pattern.search(ins) and count<64:
            print(fn)
            for a,t in hist:print(f"  {a:08X} {t}")
            print(f"> {va:08X} {ins}")
            count+=1
        hist.append((va,ins))
        previous=fn
    print(f"D3D VIEWPORT CANDIDATES REPORTED: {count}")
if __name__=="__main__":main()
