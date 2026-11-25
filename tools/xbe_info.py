#!/usr/bin/env python3
"""Inspect a *local* original Xbox XBE; outputs metadata, never game data."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

# Original Xbox retail and debug entry-point XOR keys.
ENTRY_KEYS = {"retail": 0xA8FC57AB, "debug": 0x94859D4B}
# Exact entry points published by Hulk Recompiled for its EU and USA targets.
HULK_RECOMP_ENTRIES = {0x0020AAC6: "eu", 0x0020A016: "usa"}


def inspect(path):
    data = Path(path).read_bytes()
    if len(data) < 0x12C or data[:4] != b"XBEH":
        raise ValueError("Invalid XBE header")
    base = struct.unpack_from("<I", data, 0x104)[0]
    image_size = struct.unpack_from("<I", data, 0x10C)[0]
    cert = struct.unpack_from("<I", data, 0x118)[0] - base
    if cert < 0 or cert + 0xB0 > len(data):
        raise ValueError("Invalid certificate offset")
    title_id = struct.unpack_from("<I", data, cert + 8)[0]
    title = data[cert + 12:cert + 92].decode("utf-16le", errors="replace").split("\0")[0]
    region = struct.unpack_from("<I", data, cert + 0xA0)[0]
    encoded = struct.unpack_from("<I", data, 0x128)[0]
    entry_candidates = {
        flavor: encoded ^ key
        for flavor, key in ENTRY_KEYS.items()
        if image_size and base <= (encoded ^ key) < base + image_size
    }
    # Do not guess the target if the entry is missing, corrupted or ambiguous.
    recomp_target = None
    if title_id == 0x56550039:
        targets = {
            HULK_RECOMP_ENTRIES[entry]
            for entry in entry_candidates.values()
            if entry in HULK_RECOMP_ENTRIES
        }
        if len(targets) == 1:
            recomp_target = targets.pop()
    return {
        "title": title,
        "title_id": f"{title_id:08X}",
        "region_mask": region,
        "entry_candidates": {flavor: f"0x{entry:08X}" for flavor, entry in entry_candidates.items()},
        "hulk_recompiled_candidate": recomp_target,
        "sha256": hashlib.sha256(data).hexdigest(),
        "size": len(data),
        "note": "An entry-point match is preliminary; this does not prove exact XBE or SDK compatibility."
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("xbe")
    args = parser.parse_args()
    print(json.dumps(inspect(args.xbe), indent=2, ensure_ascii=False))
