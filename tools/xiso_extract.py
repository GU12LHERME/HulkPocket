#!/usr/bin/env python3
"""Extract root default.xbe from an Xbox XISO, without changing the image."""
import argparse
import struct
from pathlib import Path
from xbe_info import inspect
import json

MAGIC = b'MICROSOFT*XBOX*MEDIA'
SECTOR = 2048
MAX_XBE = 128 * 1024 * 1024

def extract(source, destination):
    with open(source, 'rb') as f:
        f.seek(0, 2)
        length = f.tell()
        def read_at(offset, size):
            if offset < 0 or size < 0 or offset + size > length:
                raise ValueError('Read outside image')
            f.seek(offset)
            data = f.read(size)
            if len(data) != size:
                raise ValueError('Truncated image')
            return data
        for base in (0, 0xFD90000):
            if base + 32 * SECTOR + SECTOR > length:
                continue
            header = read_at(base + 32 * SECTOR, SECTOR)
            if header[:20] == MAGIC:
                break
        else:
            raise ValueError('Unsupported Xbox image layout')
        sector, size = struct.unpack_from('<II', header, 20)
        if not 14 <= size <= 16 * 1024 * 1024:
            raise ValueError('Invalid root directory size')
        directory = read_at(base + sector * SECTOR, size)
        pending = [0]
        visited = set()
        while pending:
            offset = pending.pop()
            if offset in visited:
                raise ValueError('Directory cycle')
            visited.add(offset)
            if offset + 14 > size:
                raise ValueError('Invalid directory entry')
            left, right, start, count, flags, n = struct.unpack_from('<HHIIBB', directory, offset)
            if offset + 14 + n > size:
                raise ValueError('Invalid entry name')
            name = directory[offset+14:offset+14+n]
            if name.lower() == b'default.xbe' and not flags & 16:
                if not 0 < count <= MAX_XBE:
                    raise ValueError('Invalid XBE size')
                data = read_at(base + start * SECTOR, count)
                if data[:4] != b'XBEH':
                    raise ValueError('Invalid executable magic')
                with open(destination, 'xb') as out:
                    out.write(data)
                return inspect(destination)
            pending.extend(x * 4 for x in (left, right) if x)
        raise ValueError('default.xbe not found')

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('image')
    p.add_argument('output', type=Path)
    a = p.parse_args()
    print(json.dumps(extract(a.image, a.output), indent=2))
