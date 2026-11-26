#!/usr/bin/env bash
# Copy the extracted probe into Termux private storage before executing.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
work="$(mktemp -d "${TMPDIR:-$HOME}/hulk-vulkan.XXXXXXXX")"
trap 'rm -rf -- "$work"' EXIT
cp "$here/hulk-vulkan-probe" "$work/"
cp -R "$here/spv" "$work/"
chmod 700 "$work/hulk-vulkan-probe"
"$work/hulk-vulkan-probe" "$work/spv" 2>&1 | tee "$HOME/hulk-vulkan-probe.txt"
