#!/usr/bin/env python3
"""Export locally generated ARM64 code and verified FVF metadata; never upload."""
import argparse
import hashlib
import os
import pathlib
import re
import subprocess
import tempfile
import zipfile

MAX_MODULE = 512 * 1024 * 1024
ALLOWED = {"libc.so","libm.so","libdl.so","liblog.so","libandroid.so","libc++_shared.so"}
def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024*1024), b""):
            h.update(chunk)
    return h.hexdigest()

def metadata(module, xbe, fvf, revision, dynamic):
    if not re.fullmatch(r"[0-9A-Fa-f]{8}", fvf) or int(fvf,16)<0x10000 or int(fvf,16)%4:
        raise ValueError("Invalid locally discovered FVF pointer")
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("Invalid source revision")
    if not 64 <= module.stat().st_size <= MAX_MODULE:
        raise ValueError("Invalid native module size")
    with module.open("rb") as f: head=f.read(20)
    if head[:6]!=b"\x7fELF\x02\x01" or int.from_bytes(head[16:18],"little")!=3 or int.from_bytes(head[18:20],"little")!=183:
        raise ValueError("Expected ARM64 ELF shared object")
    needed=set(re.findall(r"\(NEEDED\).*?\[([^\]]+)\]",dynamic))
    if not needed or needed-ALLOWED:
        raise ValueError("APK cannot resolve module dependencies: "+", ".join(sorted(needed-ALLOWED or {"missing NEEDED inventory"})))
    return ("format=1\nrevision="+revision+"\nfvf="+fvf.upper()+"\nxbe_sha256="+sha(xbe)+
            "\nmodule_sha256="+sha(module)+"\n").encode("ascii")

def export(module,xbe,fvf,revision,readelf,dest):
    dynamic=subprocess.check_output([str(readelf),"-d",str(module)],text=True)
    manifest=metadata(module,xbe,fvf,revision,dynamic)
    dest.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=dest.parent,delete=False) as f: tmp=pathlib.Path(f.name)
    try:
        with zipfile.ZipFile(tmp,"w",compression=zipfile.ZIP_DEFLATED,compresslevel=3) as z:
            z.writestr("preview.properties",manifest)
            z.write(module,"libhulk-code.so")
        os.replace(tmp,dest)
    finally: tmp.unlink(missing_ok=True)
    print("Local preview package:",dest,flush=True)

def self_test():
    with tempfile.TemporaryDirectory() as d:
        p=pathlib.Path(d);m=p/"game.so";x=p/"default.xbe"
        h=bytearray(64);h[:6]=b"\x7fELF\x02\x01";h[16]=3;h[18]=183;m.write_bytes(h);x.write_bytes(b"fixture")
        args=(m,x,"0039A000","a"*40)
        good="(NEEDED) Shared library: [libc.so]\n(NEEDED) Shared library: [libc++_shared.so]"
        assert b"fvf=0039A000\n" in metadata(*args,good)
        for bad in (good+"\n(NEEDED) Shared library: [libtermux.so]", ""):
            try: metadata(*args,bad)
            except ValueError: pass
            else: raise AssertionError("Unsupported dependency accepted")

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--self-test",action="store_true")
    for k in ("module","xbe","readelf","output"):p.add_argument("--"+k,type=pathlib.Path)
    p.add_argument("--fvf");p.add_argument("--revision")
    a=p.parse_args()
    if a.self_test:self_test()
    else:export(a.module,a.xbe,a.fvf,a.revision,a.readelf,a.output)
