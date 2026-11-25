#!/usr/bin/env python3
"""Verify a locally extracted XBE against the executable tested on Retroid Pocket 6.

Only reads the local file and prints metadata. Never uploads or copies game data.
A match establishes file identity, not Thunderclap engine compatibility.
"""
import argparse
import json
from pathlib import Path

from xbe_info import inspect

REFERENCE_TITLE_ID = "56550039"
REFERENCE_SHA256 = "4f233ee49f8e675d88ed54075ad4e270387798435e7a3f4a66836d0ec0e768ee"
REFERENCE_SIZE = 5197824


def verify(path: Path) -> dict:
    info = inspect(path)
    checks = {
        "title_id": info["title_id"].upper() == REFERENCE_TITLE_ID,
        "executable_size": info["size"] == REFERENCE_SIZE,
        "sha256": info["sha256"].lower() == REFERENCE_SHA256,
    }
    return {
        "checks": checks,
        "matches_rp6_executable": all(checks.values()),
        "reference": {
            "title_id": REFERENCE_TITLE_ID,
            "size": REFERENCE_SIZE,
            "sha256": REFERENCE_SHA256
        },
        "local": {
            "title_id": info["title_id"],
            "size": info["size"],
            "sha256": info["sha256"]
        },
        "note": "Exact XBE identity only; not a guarantee of Thunderclap compatibility."
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("xbe", type=Path)
    args = parser.parse_args()
    try:
        report = verify(args.xbe)
    except (OSError, ValueError) as error:
        print(json.dumps({"matches_rp6_executable": False, "error": str(error)}))
        return 1
    print(json.dumps(report, indent=2))
    return 0 if report["matches_rp6_executable"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
