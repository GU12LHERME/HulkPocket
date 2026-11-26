#!/usr/bin/env python3
"""Seed missing Xbox functions locally; retain unchanged compiled partitions."""
import argparse
import hashlib
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import tomllib
from build_xbe_android import generated_sources
from prepare_xbe_elf import prepare
from recompile_hulk_local import inspect_xbe, config_for_region

PART=re.compile(r"x86_recomp\.(\d+)\.cpp$")
BODY=re.compile(r"(?m)^X86_FUNC\(__imp__(\w+)\) \{\n")
DECL=re.compile(r"X86_EXTERN_FUNC\(\w+\);")
PREAMBLE='#include "x86_recomp_shared.h"\n\n'

def split(path):
    source=path.read_text()
    entries=list(BODY.finditer(source))
    if not source.startswith(PREAMBLE) or (entries and entries[0].start()!=len(PREAMBLE)):
        raise ValueError("Unexpected partition format: "+path.name)
    if not entries and source!=PREAMBLE:
        raise ValueError("Unrecognized empty partition: "+path.name)
    result={}
    for i,m in enumerate(entries):
        end=entries[i+1].start() if i+1<len(entries) else len(source)
        name=m[1]
        if name in result: raise ValueError("Duplicate function "+name)
        result[name]=source[m.start():end]
    return result

def parts(folder):
    return sorted((p for p in folder.glob("x86_recomp.*.cpp") if PART.fullmatch(p.name)),
                  key=lambda p:int(PART.fullmatch(p.name)[1]))

def rebalance(base, raw, output):
    """Use original partition boundaries, revised function bodies, and append new bodies."""
    revised={}
    for path in parts(raw):
        for name,body in split(path).items():
            if name in revised: raise ValueError("Duplicate revised function "+name)
            revised[name]=body
    if not revised: raise ValueError("No revised functions")
    old=parts(base)
    if not old: raise ValueError("No original partitions")
    seen=set()
    output.mkdir()
    unchanged=0
    for path in old:
        names=split(path)
        if seen.intersection(names): raise ValueError("Duplicate original function")
        seen.update(names)
        content=PREAMBLE+"".join(revised.pop(n) for n in names if n in revised)
        (output/path.name).write_text(content)
        unchanged+=content==path.read_text()
    number=max(int(PART.fullmatch(p.name)[1]) for p in old)+1
    if revised:
        # Bound each new partition without splitting a function.
        chunk=[];size=0
        for body in revised.values():
            if chunk and size+len(body)>1500000:
                (output/f"x86_recomp.{number}.cpp").write_text(PREAMBLE+"".join(chunk))
                number+=1;chunk=[];size=0
            chunk.append(body);size+=len(body)
        if chunk:(output/f"x86_recomp.{number}.cpp").write_text(PREAMBLE+"".join(chunk))
    for path in raw.iterdir():
        if path.is_file() and not PART.fullmatch(path.name):
            shutil.copyfile(path,output/path.name)
    print(f"Unchanged original partitions: {unchanged}/{len(old)}",flush=True)

def compatible_headers(old,new):
    def read(folder):
        file=folder/"x86_recomp_shared.h"
        lines=file.read_text().splitlines()
        return ([l for l in lines if not DECL.fullmatch(l)],
                {l for l in lines if DECL.fullmatch(l)})
    a,ad=read(old);b,bd=read(new)
    if a!=b or not ad<=bd:return False
    # Generated symbol metadata and both SDK headers must be byte-identical.
    for name in ("image_symbols.h","cpu/x86_context.h","cpu/x86_ops.h"):
        x,y=old/name,new/name
        if x.exists()!=y.exists() or (x.exists() and x.read_bytes()!=y.read_bytes()):return False
    return True

def fingerprint(folder,compiler):
    h=hashlib.sha256()
    h.update(subprocess.check_output([str(compiler),"--version"]))
    h.update(str(compiler.resolve()).encode())
    for f in sorted(folder.rglob("*")):
        if f.is_file():
            h.update(str(f.relative_to(folder)).encode())
            h.update(hashlib.sha256(f.read_bytes()).digest())
    h.update(pathlib.Path(__file__).with_name("build_xbe_android.py").read_bytes())
    return h.hexdigest()

def seed_config(config, seeds):
    original=tomllib.loads(config)
    existing={int(k,0) for k in original.get("functions",{})}
    lines="".join(f'0x{x:08X} = ""\n' for x in sorted(set(seeds)-existing))
    if not lines:return config
    if "[functions]\n" not in config:raise ValueError("Missing [functions] in upstream config")
    result=config.replace("[functions]\n","[functions]\n"+lines,1)
    tomllib.loads(result)
    return result

def valid_seed(xbe,seed):
    import struct
    data=xbe.read_bytes()
    base,count,table=(struct.unpack_from("<I",data,o)[0] for o in (0x104,0x11c,0x120))
    table-=base
    if not 0<count<=512 or table<0 or table+count*56>len(data):
        raise ValueError("Invalid XBE sections")
    for i in range(count):
        flags,va,size,raw,n=struct.unpack_from("<5I",data,table+i*56)
        if flags&4 and va<=seed<va+min(size,n) and raw+n<=len(data):
            return
    raise ValueError(f"Seed is not file-backed executable code: {seed:08X}")

def main():
    home=pathlib.Path.home()
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--seed",type=lambda s:int(s,16),action="append",required=True)
    p.add_argument("--recompiler",type=pathlib.Path,required=True)
    p.add_argument("--base",type=pathlib.Path,default=home/"hulk-generated")
    p.add_argument("--output",type=pathlib.Path,required=True)
    p.add_argument("--sdk",type=pathlib.Path,default=home/"XboxXBERecomp")
    p.add_argument("--upstream",type=pathlib.Path,default=home/"hulk-ultimate-destruction-recomp-xbox-ver-")
    p.add_argument("--xbe",type=pathlib.Path,default=home/"default.xbe")
    p.add_argument("--cache",type=pathlib.Path,default=home/"hulk-object-cache")
    p.add_argument("--ndk-bin",type=pathlib.Path,default=pathlib.Path(os.environ.get("PREFIX","/usr"))/"bin")
    a=p.parse_args()
    if a.output.resolve()==a.base.resolve():raise ValueError("Output must differ from baseline")
    _,_,region=inspect_xbe(a.xbe)
    for seed in a.seed:valid_seed(a.xbe,seed)
    compiler=a.ndk_bin/"clang++"
    target=subprocess.check_output([str(compiler),"-dumpmachine"],text=True).strip()
    if not target.startswith("aarch64") or "android" not in target:raise ValueError("Android ARM64 compiler required")
    cfg=a.base/"hulk-seeded-config.toml"
    if not cfg.is_file():cfg=config_for_region(a.upstream,region)
    config=seed_config(cfg.read_text(),a.seed)
    with tempfile.TemporaryDirectory(prefix="hulk-extend-") as tmp:
        work=pathlib.Path(tmp)
        original=work/"old"
        prepare(a.base,a.sdk,original)
        old_cache=a.cache/fingerprint(original,compiler)
        if not all((old_cache/(s.stem+".o")).is_file() for s in generated_sources(original)):
            raise RuntimeError("Baseline compiled cache incomplete; refusing unnecessary full rebuild")
        if a.output.exists():
            saved=a.output/"hulk-seeded-config.toml"
            if not saved.is_file() or saved.read_text()!=config:
                raise ValueError("Output exists with a different config; choose a new output")
        else:
            config_path=work/"seeded.toml";config_path.write_text(config)
            raw=work/"generated";raw.mkdir()
            subprocess.run([str(a.recompiler),str(config_path),str(a.xbe),str(raw),
                            "--xdk",str(a.sdk/"xdk")],check=True)
            mapping=(raw/"x86_func_mapping.cpp").read_text()
            for seed in a.seed:
                if not re.search(r"\{\s*0x0*"+f"{seed:X}"+r"[uU]?\s*,",mapping,re.I):
                    raise RuntimeError(f"Seed not mapped by recompiler: {seed:08X}")
            staged=a.output.with_name(a.output.name+".partial")
            if staged.exists():raise ValueError("Staging folder exists; inspect it first")
            rebalance(a.base,raw,staged)
            (staged/"hulk-seeded-config.toml").write_text(config)
            staged.rename(a.output)
        revised=work/"new"
        prepare(a.output,a.sdk,revised)
        if not compatible_headers(original,revised):
            raise RuntimeError("Shared headers changed beyond added declarations; refusing unsafe cache reuse")
        new_cache=a.cache/fingerprint(revised,compiler)
        new_cache.mkdir(parents=True,exist_ok=True)
        reuse=0;todo=[]
        for source in generated_sources(revised):
            dest=new_cache/(source.stem+".o")
            if dest.is_file():reuse+=1;continue
            old_source=original/source.name
            old_obj=old_cache/dest.name
            if old_source.is_file() and old_obj.is_file() and old_source.read_bytes()==source.read_bytes():
                try:os.link(old_obj,dest)
                except OSError:shutil.copyfile(old_obj,dest)
                reuse+=1;continue
            todo.append((source,dest))
        print(f"Reused objects: {reuse}; objects requiring compilation: {len(todo)}",flush=True)
        for i,(source,dest) in enumerate(todo,1):
            print(f"[{i}/{len(todo)}] compiling {source.name}",flush=True)
            pending=dest.with_suffix(".partial")
            subprocess.run([str(compiler),"-std=c++20","-O2","-fPIC",
                            "-fno-strict-aliasing","-Wall","-Wextra",
                            "-Wno-unused-parameter","-Wno-unused-variable","-Wno-unused-but-set-variable",
                            "-I",str(revised),"-c",str(source),"-o",str(pending)],check=True)
            pending.replace(dest)
    print("Incremental generated source and object cache ready:",a.output,flush=True)

def self_test():
    def f(n,v):
        return f"X86_FUNC(__imp__sub_{n:08X}) {{\n ctx.eax={v};\n}}\nX86_WEAK_FUNC(sub_{n:08X}, __imp__sub_{n:08X})\n\n"
    with tempfile.TemporaryDirectory() as tmp:
        root=pathlib.Path(tmp);a=root/"a";b=root/"b";c=root/"c";a.mkdir();b.mkdir()
        (a/"x86_recomp.0.cpp").write_text(PREAMBLE+f(1,1))
        (a/"x86_recomp.1.cpp").write_text(PREAMBLE+f(3,3))
        (b/"x86_recomp.0.cpp").write_text(PREAMBLE+f(1,1)+f(2,2)+f(3,4))
        rebalance(a,b,c)
        assert (c/"x86_recomp.0.cpp").read_text()==(a/"x86_recomp.0.cpp").read_text()
        assert (c/"x86_recomp.1.cpp").read_text()==PREAMBLE+f(3,4)
        assert (c/"x86_recomp.2.cpp").read_text()==PREAMBLE+f(2,2)
        for folder,decl in [(a,"X86_EXTERN_FUNC(sub_00000001);\n"),(b,"X86_EXTERN_FUNC(sub_00000002);\nX86_EXTERN_FUNC(sub_00000001);\n")]:
            (folder/"x86_recomp_shared.h").write_text("#pragma once\n"+decl)
        assert compatible_headers(a,b) and not compatible_headers(b,a)
        (b/"x86_recomp_shared.h").write_text("#pragma once\n#define CHANGED 1\nX86_EXTERN_FUNC(sub_00000001);\n")
        assert not compatible_headers(a,b)
        cfg=seed_config('[functions]\n0x1000 = ""\n[symbols]\n', [0x1000,0x2000])
        assert set(tomllib.loads(cfg)["functions"])=={"0x1000","0x00002000"}
    print("Incremental partition and cache safety tests passed")

if __name__=="__main__":
    import sys
    if sys.argv[1:]==["--self-test"]:self_test()
    else:main()
