#!/usr/bin/env python3
"""Extract an owned local XISO into a new private disc directory."""
import argparse
import hashlib
import shutil
import struct
import tempfile
from pathlib import Path
from xiso_extract import MAGIC, SECTOR

def digest(path):
    h=hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda:f.read(1024*1024),b""):h.update(chunk)
    return h.digest()

def extract_disc(source, dest, reference):
    source=Path(source).expanduser().resolve(strict=True)
    reference=Path(reference).expanduser().resolve(strict=True)
    dest=Path(dest).expanduser()
    if dest.exists() or dest.is_symlink():raise FileExistsError(dest)
    dest=dest.resolve()
    dest.parent.mkdir(parents=True,exist_ok=True)
    with source.open("rb") as f:
        f.seek(0,2);length=f.tell()
        def bounds(offset,count):
            if offset<0 or count<0 or offset+count>length:
                raise ValueError("Extent outside XISO")
        def read(offset,count):
            bounds(offset,count);f.seek(offset);data=f.read(count)
            if len(data)!=count:raise ValueError("Truncated XISO")
            return data
        for base in (0,0xFD90000):
            if base+33*SECTOR<=length:
                header=read(base+32*SECTOR,SECTOR)
                if header[:20]==MAGIC:break
        else:raise ValueError("Unsupported XISO layout")
        root_sector,root_size=struct.unpack_from("<II",header,20)
        if root_size<14:raise ValueError("Invalid root directory")
        files=[];dirs=[];seen_dirs=set();total=0;entries=0
        def walk(sector,size,relative,depth=0):
            nonlocal entries,total
            if depth>64 or size>16*1024*1024:raise ValueError("Directory limit exceeded")
            if not size:return
            key=(sector,size)
            if key in seen_dirs:raise ValueError("Repeated directory extent")
            seen_dirs.add(key)
            table=read(base+sector*SECTOR,size)
            pending=[0];seen=set();names=set()
            while pending:
                off=pending.pop()
                if off in seen or off+14>size:raise ValueError("Invalid directory tree")
                seen.add(off)
                left,right,start,count,flags,n=struct.unpack_from("<HHIIBB",table,off)
                if not n or off+14+n>size:raise ValueError("Invalid entry name length")
                name=table[off+14:off+14+n].decode("latin-1")
                if name in (".","..") or any(c in name for c in "/\\:\0") or any(ord(c)<32 for c in name):
                    raise ValueError("Unsafe XISO path")
                if name.casefold() in names:raise ValueError("Duplicate directory entry")
                names.add(name.casefold())
                path=relative/name;entries+=1
                if entries>200000:raise ValueError("Entry limit exceeded")
                bounds(base+start*SECTOR,count)
                if flags&16:
                    dirs.append(path);walk(start,count,path,depth+1)
                else:
                    total+=count
                    if total>length:raise ValueError("Total file extents exceed image size")
                    files.append((path,base+start*SECTOR,count))
                pending.extend(x*4 for x in (left,right) if x)
        walk(root_sector,root_size,Path())
        roots=[p for p,_,_ in files if len(p.parts)==1 and p.name.lower()=="default.xbe"]
        if len(roots)!=1:raise ValueError("Missing unique root default.xbe")
        if shutil.disk_usage(dest.parent).free<total+64*1024*1024:
            raise OSError("Insufficient space for extracted disc")
        stage=Path(tempfile.mkdtemp(prefix=".hulk-disc-",dir=dest.parent))
        try:
            for path in dirs:(stage/path).mkdir(parents=True,exist_ok=True)
            for i,(path,offset,count) in enumerate(files,1):
                f.seek(offset)
                with (stage/path).open("xb") as out:
                    remaining=count
                    while remaining:
                        chunk=f.read(min(remaining,1024*1024))
                        if not chunk:raise ValueError("Truncated file extent")
                        out.write(chunk);remaining-=len(chunk)
                if i%100==0:print(f"Extracted {i}/{len(files)} files",flush=True)
            if digest(stage/roots[0])!=digest(reference):
                raise ValueError("Disc default.xbe differs from the locally recompiled executable")
            if dest.exists() or dest.is_symlink():raise FileExistsError(dest)
            stage.rename(dest)
            print(f"DISC READY: {dest} files={len(files)} bytes={total}",flush=True)
        finally:
            if stage.exists():shutil.rmtree(stage)
    return dest

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("image");p.add_argument("destination")
    p.add_argument("--reference",default=str(Path.home()/"default.xbe"))
    a=p.parse_args()
    try:extract_disc(a.image,a.destination,a.reference)
    except (OSError,ValueError) as e:p.exit(1,f"Disc extraction failed: {e}\n")
