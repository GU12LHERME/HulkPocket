#!/usr/bin/env python3
"""Report local generated instruction candidates; no address patching."""
import collections
import pathlib
import re
import sys
from discover_hulk_fvf import records

def caller_context(folder, callers):
    # Return addresses identify instruction boundaries after a call.
    # Show bounded context even when the caller is outside the XDK range.
    ipat=re.compile(r"^\s*//\s+([0-9a-fA-F]{8})\s+(.*)")
    fnpat=re.compile(r"X86_FUNC\(([^)]+)\)")
    seen=set()
    for path in sorted(folder.glob("x86_recomp.*.cpp")):
        history=collections.deque(maxlen=10)
        fn=""
        after=0
        with path.open(encoding="utf-8",errors="replace") as src:
            for line in src:
                m=fnpat.search(line)
                if m:
                    fn=m[1];history.clear();after=0
                m=ipat.match(line)
                if not m:continue
                va=int(m[1],16);ins=m[2]
                if after:
                    print(f"  {va:08X} {ins}");after-=1
                if va in callers and va not in seen:
                    seen.add(va)
                    print(f"D3D VIEWPORT CALLER CONTEXT {va:08X} {fn} ({path.name})")
                    for a,t in history:print(f"  {a:08X} {t}")
                    print(f"> {va:08X} {ins}")
                    after=6
                history.append((va,ins))
    for va in sorted(callers-seen):print(f"D3D VIEWPORT CALLER NOT FOUND {va:08X}")

def main():
    folder=pathlib.Path(sys.argv[1])
    if len(sys.argv)>2:
        log=pathlib.Path(sys.argv[2]).read_text(errors="replace")
        callers=set()
        for row in log.splitlines():
            if row.startswith("D3D VIEWPORT CHANGE "):
                callers.update(int(x,16) for x in re.findall(r"(?:previous|current)_caller=([0-9A-Fa-f]{8})",row) if int(x,16))
        caller_context(folder,set(sorted(callers)[:16]))
    # CDevice USA 0039DFB0 + viewport fields EE0..EF4. Register-relative
    # references are candidates only; their base register is not proven.
    pattern=re.compile(r"\b(?:0x0*(?:ee0|ee4|ee8|eec|ef0|ef4|39ee90|39ee94|39ee98|39ee9c|39eea0|39eea4)|(?:ee0|ee4|ee8|eec|ef0|ef4|39ee90|39ee94|39ee98|39ee9c|39eea0|39eea4)h)\b",re.I)
    hist=collections.deque(maxlen=5)
    previous=None
    count=0
    print("D3D VIEWPORT LOCAL INSTRUCTION CANDIDATES (unverified register bases)")
    for fn,va,ins in records(folder):
        if fn!=previous:hist.clear()
        if pattern.search(ins) and count<64:
            print(fn)
            for a,t in hist:print(f"  {a:08X} {t}")
            print(f"> {va:08X} {ins}")
            count+=1
        hist.append((va,ins))
        previous=fn
    print(f"D3D VIEWPORT CANDIDATES REPORTED: {count}")
    # The actual six stores in the latest report identify this function.
    # Include its complete bounded local instruction context and callers.
    print("D3D VIEWPORT WRITER LOCAL BODY (0037F720)")
    seen=set()
    for fn,va,ins in records(folder):
        if fn=="__imp__sub_0037F720" and va not in seen and len(seen)<260:
            print(f"  {va:08X} {ins}");seen.add(va)
    print(f"D3D VIEWPORT WRITER BODY INSTRUCTIONS: {len(seen)}")
    print("D3D VIEWPORT WRITER DIRECT CALL REFERENCES (bounded)")
    count=0
    for fn,va,ins in records(folder):
        if re.search(r"\bcall\s+0x0*37f720\b",ins,re.I) and count<24:
            print(f"{fn} {va:08X} {ins}");count+=1
if __name__=="__main__":main()
