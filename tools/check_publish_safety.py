#!/usr/bin/env python3
"""Refuse to commit or publish game media, recompiled modules, binaries or secrets.

Modes:
  --staged   check files staged for commit (used by .githooks/pre-commit)
  --tree     check every tracked file (used by CI)
  --self-test

Detection is by path/extension, size and content signatures (XBE/ELF/PE
headers, private keys, common token formats). It never prints file contents.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys

MAX_BYTES = 5 * 1024 * 1024
BLOCKED_SUFFIXES = {
    # Game media and data
    ".iso", ".xiso", ".xbe", ".chd", ".cso", ".xbx", ".xmv", ".xpr", ".xwb", ".xsb", ".xwm", ".bik",
    # Native binaries / packages (recompiled modules end up here)
    ".so", ".o", ".obj", ".a", ".lib", ".dll", ".dylib", ".exe", ".apk", ".aab", ".dex",
    # Credentials
    ".keystore", ".jks", ".pem", ".p12", ".pfx",
}
BLOCKED_NAMES = {
    "default.xbe", "local.properties", "key.properties", "keystore.properties",
    "google-services.json", ".env", "settings.local.json", "image_symbols.h",
}
BLOCKED_PARTS = {"private_generated", "hulk-generated", "hulk-object-cache", "TDATA", "UDATA"}
GENERATED_RECOMP = re.compile(r"x86_recomp\.[0-9]+\.cpp\Z")
SIGNATURES = (
    (b"XBEH", "Xbox XBE executable header"),
    (b"\x7fELF", "ELF binary (compiled or recompiled module)"),
    (b"MZ", "Windows PE binary"),
)
SECRET_PATTERNS = (
    (re.compile(rb"-----BEGIN [A-Z ]*PRIVATE KEY-----"), "private key"),
    (re.compile(rb"\bgh[pousr]_[A-Za-z0-9]{36,}\b"), "GitHub token"),
    (re.compile(rb"\bgithub_pat_[A-Za-z0-9_]{60,}\b"), "GitHub fine-grained token"),
    (re.compile(rb"\bAKIA[0-9A-Z]{16}\b"), "AWS access key"),
    (re.compile(rb"\bsk-ant-[A-Za-z0-9_-]{20,}"), "Anthropic API key"),
    (re.compile(rb"\bxox[baprs]-[A-Za-z0-9-]{10,}"), "Slack token"),
)
# Paths whose purpose is to contain these patterns as test fixtures.
SELF_EXEMPT = {"tools/check_publish_safety.py", "tools/test_check_publish_safety.py"}


def classify(path: str, data: bytes) -> list[str]:
    problems = []
    p = pathlib.PurePosixPath(path)
    if p.suffix.lower() in BLOCKED_SUFFIXES:
        problems.append(f"blocked file type {p.suffix}")
    if p.name in BLOCKED_NAMES or p.name.startswith(".env."):
        problems.append("blocked file name")
    if BLOCKED_PARTS.intersection(p.parts):
        problems.append("private game data or generated-code directory")
    if GENERATED_RECOMP.match(p.name) or p.name == "x86_func_mapping.cpp":
        problems.append("recompiled game code")
    if len(data) > MAX_BYTES:
        problems.append(f"larger than {MAX_BYTES // (1024 * 1024)} MiB")
    for magic, label in SIGNATURES:
        if data.startswith(magic) and (magic != b"MZ" or b"PE\0\0" in data[:1024]):
            problems.append(label)
    if path not in SELF_EXEMPT:
        for pattern, label in SECRET_PATTERNS:
            if pattern.search(data):
                problems.append(f"possible {label}")
    return problems


def git(*args: str) -> bytes:
    return subprocess.check_output(["git", *args])


def staged() -> list[tuple[str, bytes]]:
    names = git("diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z").split(b"\0")
    out = []
    for raw in filter(None, names):
        name = raw.decode("utf-8", "surrogateescape")
        out.append((name, git("show", f":{name}")))
    return out


def tracked() -> list[tuple[str, bytes]]:
    out = []
    for raw in filter(None, git("ls-files", "-z").split(b"\0")):
        name = raw.decode("utf-8", "surrogateescape")
        path = pathlib.Path(name)
        if path.is_file():
            out.append((name, path.read_bytes()))
    return out


def report(entries: list[tuple[str, bytes]]) -> int:
    failures = 0
    for name, data in entries:
        problems = classify(name, data)
        if problems:
            failures += 1
            print(f"BLOCKED {name}: {', '.join(problems)}", file=sys.stderr)
    if failures:
        print(f"{failures} file(s) must not be committed or published. "
              "Remove them from the index (git rm --cached <file>).", file=sys.stderr)
        return 1
    return 0


def self_test() -> None:
    assert classify("README.md", b"# HulkPocket\n") == []
    assert classify("tools/foo.py", b"print('ok')\n") == []
    assert classify("game/default.xbe", b"XBEH....")
    assert classify("isos/hulk.iso", b"")
    assert classify("out/libmain.so", b"\x7fELF\x02")
    assert classify("misc/blob.bin", b"\x7fELF\x02\x01")
    assert classify("private_generated/x86_recomp.12.cpp", b"int x;")
    assert classify("android/local.properties", b"sdk.dir=C:\\\\Android")
    assert classify("notes.txt", b"token ghp_" + b"a" * 36)
    assert classify("k.txt", b"-----BEGIN RSA PRIVATE KEY-----")
    assert classify("big.txt", b"a" * (MAX_BYTES + 1))
    assert classify("doc.txt", b"MZ is fine without a PE header") == []
    print("Publish safety tests passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--staged", action="store_true")
    group.add_argument("--tree", action="store_true")
    group.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    return report(staged() if args.staged else tracked())


if __name__ == "__main__":
    raise SystemExit(main())
