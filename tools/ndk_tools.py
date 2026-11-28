"""Resolve Android NDK / LLVM tool paths on Linux, macOS, Termux and Windows.

On Windows the NDK ships clang/llvm tools as ``.exe`` and the API-level
wrappers (``aarch64-linux-android29-clang++``) as ``.cmd`` batch files, so a
bare POSIX name never exists on disk there.
"""
from __future__ import annotations

import os
import pathlib

# Host directory names under toolchains/llvm/prebuilt, in preference order.
NDK_HOSTS = ("linux-x86_64", "darwin-x86_64", "darwin-arm64", "windows-x86_64")
WINDOWS_SUFFIXES = (".exe", ".cmd", ".bat")


def tool(bin_dir: pathlib.Path, name: str, windows: bool | None = None) -> pathlib.Path:
    """Return the existing path for ``name`` in ``bin_dir``.

    Falls back to Windows suffixes when the bare name is missing. If nothing
    exists, the bare path is returned so callers keep their existing
    "missing tool" error handling.
    """
    bare = bin_dir / name
    if bare.is_file():
        return bare
    if windows is None:
        windows = os.name == "nt"
    if windows:
        for suffix in WINDOWS_SUFFIXES:
            candidate = bin_dir / (name + suffix)
            if candidate.is_file():
                return candidate
    return bare


def ndk_bin_dirs(ndk: pathlib.Path) -> list[pathlib.Path]:
    return [ndk / "toolchains/llvm/prebuilt" / host / "bin" for host in NDK_HOSTS]


def has_ndk_clang(ndk: pathlib.Path) -> bool:
    return any(tool(bin_dir, "clang", windows=True).is_file() for bin_dir in ndk_bin_dirs(ndk))
