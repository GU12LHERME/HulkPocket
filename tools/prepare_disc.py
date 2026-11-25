#!/usr/bin/env python3
"""Safely unpack a *local* original Xbox XISO for Thunderclap.

Requires the independently installed xdvdfs CLI. Game content is never
uploaded, printed, embedded in an APK, or added to Git. Extraction is
transactional: incomplete output is removed on failure.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from check_game_version import verify as verify_game_version


def extract(iso: Path, dest: Path, executable="xdvdfs"):
    iso, dest = iso.expanduser().resolve(), dest.expanduser().resolve()
    if not iso.is_file():
        raise ValueError("ISO not found or not a regular file")
    if dest.exists() or dest.is_symlink():
        raise ValueError("Destination already exists; refusing to overwrite it")
    if iso == dest or iso in dest.parents:
        raise ValueError("Destination must not be inside the ISO source path")
    if not shutil.which(executable):
        raise ValueError("xdvdfs CLI is not installed or not on PATH")
    dest.parent.mkdir(parents=True, exist_ok=True)
    # Work under the final destination's parent to allow an atomic rename.
    temp = Path(tempfile.mkdtemp(prefix=".hulkpocket-extract-", dir=dest.parent))
    try:
        command = [executable, "unpack", str(iso), str(temp)]
        p = subprocess.run(command, check=False)
        if p.returncode:
            raise RuntimeError(f"xdvdfs extraction failed (code {p.returncode})")
        xbe = temp / "default.xbe"
        # xdvdfs is expected to put the root XBE directly in the output dir.
        if xbe.is_symlink() or not xbe.is_file():
            raise RuntimeError("Extracted root default.xbe is missing or unsafe")
        result = verify_game_version(xbe)
        if not result["matches_rp6_executable"]:
            raise RuntimeError("Extracted XBE does not match tested RP6 copy")
        if dest.exists() or dest.is_symlink():
            raise RuntimeError("Destination appeared while extracting")
        temp.rename(dest)
        return {
            "verified": True,
            "destination": str(dest),
            "title_id": result["reference"]["title_id"],
            "xbe_sha256": result["reference"]["sha256"],
            "note": "Game assets stay in local storage only."
        }
    finally:
        if temp.exists():
            shutil.rmtree(temp)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("iso", type=Path, help="locally owned Xbox ISO/XISO")
    p.add_argument("destination", type=Path, help="new directory outside tracked sources")
    args = p.parse_args()
    try:
        print(json.dumps(extract(args.iso, args.destination), indent=2))
        return 0
    except (OSError, ValueError, RuntimeError) as error:
        print(f"Disc preparation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
