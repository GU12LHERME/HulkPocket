#!/usr/bin/env python3
"""Fetch the pinned XboxXBERecomp runtime ABI header and verify its git blob hash.

Mirrors the "Fetch pinned SDK context" step used by the GitHub Actions
workflows, but runs on Windows, Linux and macOS with only the standard library.
The resulting directory is what CMake expects as HULK_SDK_DIR and what the
Android Gradle build reads from android/app/src/main/cpp/sdk.
"""
import argparse
import hashlib
import os
import pathlib
import sys
import tempfile
import urllib.request

COMMIT = "767a81837fba2d86913f6af81a10b33661d35518"
RELATIVE = "runtime/cpu/x86_context.h"
URL = f"https://raw.githubusercontent.com/TGP482/XboxXBERecomp/{COMMIT}/{RELATIVE}"
BLOB_SHA1 = "a02e10b8e2069920529d79d1f4e2413c73cb5f10"
DEFAULT_DEST = pathlib.Path(__file__).resolve().parent.parent / "android/app/src/main/cpp/sdk"


def git_blob_sha1(data: bytes) -> str:
    """Same value as `git hash-object` for the given bytes (no filters)."""
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def fetch(dest: pathlib.Path, retries: int = 3) -> pathlib.Path:
    target = dest / "cpu" / "x86_context.h"
    if target.is_file() and git_blob_sha1(target.read_bytes()) == BLOB_SHA1:
        return target
    last_error = None
    for _ in range(retries):
        try:
            with urllib.request.urlopen(URL, timeout=30) as response:
                data = response.read()
            break
        except OSError as error:
            last_error = error
    else:
        raise RuntimeError(f"Download failed: {last_error}")
    actual = git_blob_sha1(data)
    if actual != BLOB_SHA1:
        raise RuntimeError(f"Hash mismatch: expected {BLOB_SHA1}, got {actual}")
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, pending = tempfile.mkstemp(dir=target.parent, prefix=".x86_context-", suffix=".partial")
    try:
        with os.fdopen(fd, "wb") as out:
            out.write(data)
        os.replace(pending, target)
    except BaseException:
        pathlib.Path(pending).unlink(missing_ok=True)
        raise
    return target


def self_test() -> None:
    # Known git hash-object values.
    assert git_blob_sha1(b"") == "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391"
    assert git_blob_sha1(b"hello\n") == "ce013625030ba8dba906f756967f9e9ca394464a"
    print("SDK context hash tests passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dest", type=pathlib.Path, default=DEFAULT_DEST,
                        help="directory that will contain cpu/x86_context.h (HULK_SDK_DIR)")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    try:
        target = fetch(args.dest)
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1
    print(f"Verified {target} ({BLOB_SHA1})")
    print(f"HULK_SDK_DIR={target.parent.parent}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
