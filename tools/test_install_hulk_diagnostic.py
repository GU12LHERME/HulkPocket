#!/usr/bin/env python3
import hashlib
import pathlib
import tempfile
import zipfile
from install_hulk_diagnostic import install,prepare_partition

with tempfile.TemporaryDirectory() as tmp:
    root=pathlib.Path(tmp)
    archive=root/"artifact.zip";dest=root/"diagnostic"
    elf=bytearray(64);elf[:6]=b"\x7fELF\x02\x01";elf[18:20]=(183).to_bytes(2,"little")
    def make(rev="test",digest=None):
        with zipfile.ZipFile(archive,"w") as z:
            z.writestr("revision.txt",rev+"\n")
            z.writestr("hulk-live-diagnostic",elf)
            z.writestr("hulk-live-diagnostic.sha256",(digest or hashlib.sha256(elf).hexdigest())+"  hulk-live-diagnostic\n")
    make();install(archive,dest,"test");assert dest.read_bytes()==elf
    for rev,digest in (("wrong",None),("test","0"*64)):
        make(rev,digest)
        try:install(archive,dest,"test")
        except ValueError:pass
        else:raise AssertionError("invalid archive accepted")
        assert dest.read_bytes()==elf
    partition=root/"private";prepare_partition(partition);prepare_partition(partition)
    assert (partition/"TDATA/56550039").is_dir()
    assert (partition/"UDATA/56550039").is_dir()
    outside=root/"outside";outside.mkdir()
    bad=root/"bad";bad.mkdir();(bad/"TDATA").symlink_to(outside,target_is_directory=True)
    try:prepare_partition(bad)
    except OSError:pass
    else:raise AssertionError("symlink accepted")
    assert not list(outside.iterdir())
print("Artifact revision/checksum and private directory preparation tests passed")
