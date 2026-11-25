#!/usr/bin/env python3
"""Local-only Xbox Hulk recompiler driver.

Uses the official recompiler and the upstream Hulk configuration, with
strict preflight on title and entrypoint. No game data is uploaded.
Output contains generated copyrighted C++: do not commit it.
"""
from __future__ import annotations

import argparse
import pathlib
import struct
import subprocess
import tomllib

TITLE_ID = 0x56550039
ENTRIES = {"eu": 0x0020AAC6, "usa": 0x0020A016}
XOR_KEYS = ((0xA8FC57AB, 0x5B6D40B6),
            (0x94859D4B, 0xEFB1F152))

def inspect_xbe(path: pathlib.Path) -> tuple[int, int, str]:
    with path.open("rb") as stream:
        header = stream.read(0x200)
        if len(header) < 0x180 or header[:4] != b"XBEH":
            raise ValueError("missing original Xbox XBE header")
        def u32(offset: int) -> int:
            return struct.unpack_from("<I", header, offset)[0]
        base, image_size, cert = u32(0x104), u32(0x10C), u32(0x118)
        if not base or not image_size or image_size > 512 * 1024 * 1024:
            raise ValueError("invalid XBE guest image size")
        if cert < base or cert - base + 12 > image_size:
            raise ValueError("invalid XBE certificate address")
        stream.seek(cert - base + 8)
        title_data = stream.read(4)
        if len(title_data) != 4:
            raise ValueError("truncated XBE title certificate")
        title = struct.unpack("<I", title_data)[0]
        if title != TITLE_ID:
            raise ValueError("not Hulk Ultimate Destruction title ID")
        candidates = []
        for entry_xor, thunk_xor in XOR_KEYS:
            entry = u32(0x128) ^ entry_xor
            thunk = u32(0x158) ^ thunk_xor
            if base <= entry < base + image_size and base <= thunk < base + image_size:
                candidates.append(entry)
        regions = [region for region, address in ENTRIES.items() if address in candidates]
        if len(regions) != 1:
            raise ValueError("unsupported/ambiguous Hulk XBE entrypoint")
        return title, ENTRIES[regions[0]], regions[0]

def config_for_region(upstream: pathlib.Path, region: str) -> pathlib.Path:
    name = "game.toml" if region == "eu" else "game_usa.toml"
    config = upstream / "config" / name
    with config.open("rb") as stream:
        doc = tomllib.load(stream)
    xbe = doc["xbe"]
    if (int(xbe["title"]) != TITLE_ID or
        int(xbe["entry"]) != ENTRIES[region] or
        int(xbe["xdk"]) != 5849):
        raise ValueError("upstream Hulk recompilation config does not match XBE")
    return config

def run(recompiler: pathlib.Path, sdk: pathlib.Path, upstream: pathlib.Path,
        original: pathlib.Path, output: pathlib.Path) -> pathlib.Path:
    _, entry, region = inspect_xbe(original)
    config = config_for_region(upstream, region)
    if not recompiler.is_file() or not (sdk / "xdk" / "5849" / "layout.toml").is_file():
        raise FileNotFoundError("missing official recompiler or SDK XDK 5849 layout")
    if output.resolve() == original.resolve() or output.resolve() == upstream.resolve():
        raise ValueError("refusing to overwrite original image or upstream source")
    output.mkdir(parents=True, exist_ok=True)
    command = [str(recompiler), str(config), str(original), str(output),
               "--xdk", str(sdk / "xdk")]
    print(f"Local recompilation target: {region.upper()} entry=0x{entry:08X}")
    subprocess.run(command, check=True)
    if not (output / "x86_func_mapping.cpp").is_file():
        raise RuntimeError("recompiler did not produce function mapping")
    if not list(output.glob("x86_recomp.*.cpp")):
        raise RuntimeError("recompiler did not produce any instruction partitions")
    print(f"Generated private game C++ in: {output}")
    return output

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xbe", type=pathlib.Path, required=True)
    parser.add_argument("--sdk", type=pathlib.Path, required=True)
    parser.add_argument("--upstream", type=pathlib.Path, required=True)
    parser.add_argument("--recompiler", type=pathlib.Path, required=True)
    parser.add_argument("--generated", type=pathlib.Path, required=True)
    a = parser.parse_args()
    run(a.recompiler, a.sdk, a.upstream, a.xbe, a.generated)

if __name__ == "__main__":
    main()
