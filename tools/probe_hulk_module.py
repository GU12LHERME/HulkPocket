#!/usr/bin/env python3
"""Load a trusted local diagnostic ELF and validate its internal dispatcher."""
import argparse
import ctypes
import pathlib

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("module", type=pathlib.Path)
    p.add_argument("--entry", required=True, type=lambda s: int(s, 0))
    a = p.parse_args()
    if not 0 < a.entry <= 0xffffffff:
        p.error("invalid guest entry")
    module = ctypes.CDLL(str(a.module.resolve()))
    probe = module.hulk_entry_probe
    probe.argtypes = [ctypes.c_uint32]
    probe.restype = ctypes.c_int
    status = probe(a.entry)
    print("Internal dispatcher status:", status, flush=True)
    if status:
        raise SystemExit(status)
    print("PASS: internal dispatcher validated; no guest instructions executed.")

if __name__ == "__main__":
    main()
