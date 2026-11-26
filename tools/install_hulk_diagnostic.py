#!/usr/bin/env python3
"""Install the matching diagnostic artifact and prepare only private Xbox dirs."""
import argparse
import hashlib
import os
import pathlib
import subprocess
import tempfile
import zipfile

def prepare_partition(root):
    flags=os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW
    root.mkdir(mode=0o700,exist_ok=True)
    fd=os.open(root,flags)
    try:
        for name in ("TDATA","UDATA"):
            try: os.mkdir(name,mode=0o700,dir_fd=fd)
            except FileExistsError: pass
            child=os.open(name,flags,dir_fd=fd)
            try:
                try: os.mkdir("56550039",mode=0o700,dir_fd=child)
                except FileExistsError: pass
                title=os.open("56550039",flags,dir_fd=child)
                os.close(title)
            finally: os.close(child)
    finally: os.close(fd)

def install(archive,destination,revision):
    with zipfile.ZipFile(archive) as z:
        names=("revision.txt","hulk-live-diagnostic.sha256","hulk-live-diagnostic")
        for name in names:
            if z.namelist().count(name)!=1:
                raise ValueError("Missing or duplicate ZIP entry: "+name)
        if z.getinfo(names[0]).file_size>128 or z.getinfo(names[1]).file_size>512:
            raise ValueError("Invalid artifact metadata")
        if z.read(names[0]).decode().strip()!=revision:
            raise ValueError("ZIP revision differs from local checkout; download matching build")
        fields=z.read(names[1]).decode().split()
        if len(fields)!=2 or fields[1]!="hulk-live-diagnostic":
            raise ValueError("Invalid binary checksum manifest")
        if not 64<=z.getinfo(names[2]).file_size<=64*1024*1024:
            raise ValueError("Unexpected diagnostic binary size")
        data=z.read(names[2])
        if hashlib.sha256(data).hexdigest()!=fields[0]:
            raise ValueError("Diagnostic SHA256 mismatch")
        if data[:6]!=b"\x7fELF\x02\x01" or int.from_bytes(data[18:20],"little")!=183:
            raise ValueError("Expected little-endian ARM64 ELF")
    with tempfile.NamedTemporaryFile(dir=destination.parent,delete=False) as tmp:
        pending=pathlib.Path(tmp.name)
        try:
            tmp.write(data);tmp.flush();os.fsync(tmp.fileno())
            os.chmod(pending,0o700)
        except BaseException:
            pending.unlink(missing_ok=True);raise
    try: os.replace(pending,destination)
    finally: pending.unlink(missing_ok=True)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("archive",type=pathlib.Path)
    p.add_argument("--disc-root",type=pathlib.Path,required=True)
    p.add_argument("--install-only",action="store_true")
    a=p.parse_args()
    repo=pathlib.Path(__file__).resolve().parent.parent
    revision=subprocess.check_output(["git","rev-parse","HEAD"],cwd=repo,text=True).strip()
    home=pathlib.Path.home()
    partition=home/"hulk-partition1"
    disc=a.disc_root.resolve(strict=True)
    if not (disc/"default.xbe").is_file():raise ValueError("Disc root must contain default.xbe")
    # Refuse overlapping roots, including a symlink to the original disc.
    resolved=partition.resolve()
    if resolved==disc or resolved in disc.parents or disc in resolved.parents:
        raise ValueError("Disc and private partition must be disjoint")
    install(a.archive,home/"hulk-live-diagnostic",revision)
    prepare_partition(partition)
    print("Verified diagnostic:",revision,flush=True)
    if not a.install_only:
        env=os.environ.copy()
        env.update(HULK_PARTITION1_ROOT=str(partition),HULK_DISC_ROOT=str(disc),
            HULK_CONTINUE_CREATE="1",HULK_CONTINUE_DRAW="1",HULK_CONTINUE_SWAP="1",
            HULK_DIAGNOSTIC_BIN=str(home/"hulk-live-diagnostic"))
        return subprocess.call(["bash","tools/run_hulk_next.sh"],cwd=repo,env=env)
    return 0
if __name__=="__main__":
    raise SystemExit(main())
