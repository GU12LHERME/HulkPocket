#!/usr/bin/env bash
# Provision a WSL2 Ubuntu 24.04 (or any Ubuntu 24.04) host with the same
# toolchain the GitHub Actions ubuntu-24.04 runners use for HulkPocket.
#
# Usage (from Windows):  wsl -d Ubuntu-24.04 -u root -- bash tools/setup_wsl.sh [user]
# Usage (inside Ubuntu): sudo bash tools/setup_wsl.sh "$USER"
#
# Installs apt packages, a Linux Android SDK with the pinned packages, and
# the NDK r28c used directly by several workflows. Safe to re-run.
set -euo pipefail

if [[ $(id -u) -ne 0 ]]; then
    echo "Run as root (sudo or wsl -u root)." >&2
    exit 2
fi
owner="${1:-${SUDO_USER:-}}"
if [[ -z "$owner" ]]; then
    owner=$(getent passwd 1000 | cut -d: -f1)
fi
home=$(getent passwd "$owner" | cut -d: -f6)
[[ -n "$home" ]] || { echo "Unknown user: $owner" >&2; exit 2; }

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends \
    build-essential clang g++-aarch64-linux-gnu qemu-user \
    cmake ninja-build ccache nasm pkg-config make meson \
    xz-utils unzip zip curl ca-certificates git file binutils \
    python3 python3-venv python3-pip python3-yaml \
    libvulkan-dev mesa-vulkan-drivers spirv-tools \
    libegl1-mesa-dev libgles2-mesa-dev libgl1-mesa-dri \
    openjdk-17-jdk-headless

# Pinned Android packages (keep in sync with .github/workflows/*.yml).
CMDLINE_TOOLS_ZIP=commandlinetools-linux-13114758_latest.zip
NDK_VERSION=28.2.13676358
NDK_R=android-ndk-r28c
sdk="$home/Android/Sdk"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if [[ ! -x "$sdk/cmdline-tools/latest/bin/sdkmanager" ]]; then
    curl --fail --location --retry 3 "https://dl.google.com/android/repository/$CMDLINE_TOOLS_ZIP" -o "$work/tools.zip"
    unzip -q "$work/tools.zip" -d "$work"
    mkdir -p "$sdk/cmdline-tools"
    rm -rf "$sdk/cmdline-tools/latest"
    mv "$work/cmdline-tools" "$sdk/cmdline-tools/latest"
fi
yes | "$sdk/cmdline-tools/latest/bin/sdkmanager" --sdk_root="$sdk" --licenses >/dev/null || true
"$sdk/cmdline-tools/latest/bin/sdkmanager" --sdk_root="$sdk" \
    platform-tools 'platforms;android-35' 'build-tools;35.0.0' "ndk;$NDK_VERSION" 'cmake;3.22.1'

# Several workflows download the r28c zip directly; keep an extracted copy
# at a stable path so the local runner can reuse it instead of re-downloading.
if [[ ! -e "$home/$NDK_R" ]]; then
    ln -s "$sdk/ndk/$NDK_VERSION" "$home/$NDK_R"
fi
chown -R "$owner:$owner" "$home/Android"
chown -h "$owner:$owner" "$home/$NDK_R"

profile="$home/.profile"
if ! grep -q 'HulkPocket toolchain' "$profile" 2>/dev/null; then
    cat >> "$profile" <<EOF
# HulkPocket toolchain (tools/setup_wsl.sh)
export ANDROID_HOME="$sdk"
export ANDROID_SDK_ROOT="$sdk"
export ANDROID_NDK_HOME="$sdk/ndk/$NDK_VERSION"
export HULK_NDK="$sdk/ndk/$NDK_VERSION"
export PATH="\$ANDROID_HOME/cmdline-tools/latest/bin:\$ANDROID_HOME/platform-tools:\$PATH"
EOF
    chown "$owner:$owner" "$profile"
fi

echo "HulkPocket WSL toolchain ready for $owner"
for tool in g++ clang aarch64-linux-gnu-g++ qemu-aarch64 cmake ninja ccache spirv-val java; do
    printf '%-22s %s\n' "$tool" "$("$tool" --version 2>&1 | head -n1)"
done
"$sdk/ndk/$NDK_VERSION/toolchains/llvm/prebuilt/linux-x86_64/bin/clang" --version | head -n1
