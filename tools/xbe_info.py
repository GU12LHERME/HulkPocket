#!/usr/bin/env python3
"""Inspect a locally extracted Xbox executable; never uploads game data."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def inspect(path):
    data = Path(path).read_bytes()
    if len(data) < 0x11c or data[:4] != b'XBEH':
        raise ValueError('Invalid XBE header')
    base = struct.unpack_from('<I', data, 0x104)[0]
    cert = struct.unpack_from('<I', data, 0x118)[0] - base
    if cert < 0 or cert + 0xb0 > len(data):
        raise ValueError('Invalid certificate offset')
    title_id = struct.unpack_from('<I', data, cert + 8)[0]
    title = data[cert+12:cert+92].decode('utf-16le', errors='replace').split('\0')[0]
    region = struct.unpack_from('<I', data, cert+0xa0)[0]
    return {'title': title, 'title_id': f'{title_id:08X}', 'region_mask': region, 'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data)}

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('xbe')
    args = parser.parse_args()
    print(json.dumps(inspect(args.xbe), indent=2, ensure_ascii=False))
