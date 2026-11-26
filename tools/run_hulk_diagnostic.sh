#!/usr/bin/env bash
# Relink only the runtime; the original Xbox game and cached objects stay local.
set -euo pipefail
cd "$(dirname "$0")/.."
sdk="${1:-$HOME/XboxXBERecomp}"
generated="${2:-$HOME/hulk-generated}"
cache="${3:-$HOME/hulk-object-cache}"
xbe="${4:-$HOME/default.xbe}"
disc="${5:-}"
out="$HOME/hulk-diagnostic-current"
module="$HOME/libhulk-code-current.so"
log="$HOME/storage/downloads/hulk-diagnostic-current.txt"
if [[ ! -d "$(dirname "$log")" ]]; then log="$HOME/hulk-diagnostic-current.txt"; fi
{
    echo "HulkPocket diagnostic revision: $(git rev-parse --short HEAD)"
    # Resolve the original FVF record before relinking or running the game.
    if [[ -z "${HULK_FVF_SHADER:-}" ]]; then
        HULK_FVF_SHADER=$(python tools/discover_hulk_fvf.py "$xbe" "$generated")
        export HULK_FVF_SHADER
    fi
    if [[ -z "$HULK_FVF_SHADER" ]]; then
        echo "FVF pointer unresolved. Send this report; no guest execution attempted."
        exit 2
    fi
    echo "Original FVF pointer candidate: $HULK_FVF_SHADER"
    python tools/link_hulk_cached.py --sdk "$sdk" --generated "$generated" \
        --cache "$cache" --ndk-bin "$PREFIX/bin" --output "$module"
    # Prefer the explicitly selected Android-NDK CI artifact when testing
    # on Termux, whose sysroot intentionally excludes EGL/GLES headers.
    # This executable must correspond to the current runtime sources.
    if [[ -n "${HULK_DIAGNOSTIC_BIN:-}" ]]; then
        out="$HULK_DIAGNOSTIC_BIN"
        if [[ ! -f "$out" || ! -x "$out" ]]; then
            echo "Selected diagnostic is not executable: $out"
            exit 2
        fi
        header=$("$PREFIX/bin/llvm-readelf" -h "$out")
        if [[ "$header" != *AArch64* || "$header" != *ELF64* ]]; then
            echo "Selected diagnostic is not ARM64 ELF"
            exit 2
        fi
        echo "Using explicitly selected prebuilt Android diagnostic: $out"
    else
        if ! printf '#include <EGL/egl.h>\n#include <GLES3/gl3.h>\n' | clang++ -x c++ -E - >/dev/null 2>&1; then
            echo "Termux EGL/GLES headers unavailable. Use the matching CI hulk-live-diagnostic artifact via HULK_DIAGNOSTIC_BIN."
            exit 2
        fi
        clang++ -std=c++20 -O2 -pthread -DHULK_ENABLE_ENTRY_DIAGNOSTIC -DHULK_ENABLE_GLES_DEVICE_PROBE \
            -Ithird_party/xboxxberecomp -I"$sdk/runtime" native/hulk_xbe_local_diagnostic.cpp \
            native/hulk_xbe_preflight.cpp native/xbox_guest_address_space.cpp \
            native/xbe_image_memory_loader.cpp native/xbe_kernel_imports.cpp \
            native/portable_xbe_image.cpp native/portable_xbe_dispatch.cpp \
            third_party/xboxxberecomp/kernel/crypto.cpp \
            native/xbe_gles_backend.cpp third_party/xboxxberecomp/gpu/texture.cpp \
            third_party/xboxxberecomp/gpu/vertex.cpp \
            -ldl -lEGL -lGLESv3 -o "$out"
    fi
    args=()
    if [[ -n "$disc" ]]; then args+=(--disc-root "$disc"); fi
    execution_log=$(mktemp)
    trap 'rm -f "$execution_log"' EXIT
    status=0
    "$out" "$xbe" "$module" --execute-diagnostic "${args[@]}" >"$execution_log" 2>&1 || status=$?
    cat "$execution_log"
    if grep -q "MISSING GUEST target=" "$execution_log"; then
        python tools/report_missing_guest.py "$xbe" "$generated" --log "$execution_log" || true
    fi
    if grep -q "HULK ALLOC caller=" "$execution_log"; then
        python tools/report_hulk_allocator.py "$generated" "$execution_log" || true
    fi
    pc=$(sed -n 's/.*pc_minus_module_base=\(0x[0-9A-Fa-f]*\).*/\1/p' "$execution_log" | tail -n 1)
    if [[ -n "$pc" && -x "$PREFIX/bin/llvm-addr2line" ]]; then
        echo "Fault native function:"
        "$PREFIX/bin/llvm-addr2line" -f -C -e "$module" "$pc" || true
        if [[ -x "$PREFIX/bin/llvm-objdump" ]]; then
            echo "Native instruction window:"
            "$PREFIX/bin/llvm-objdump" -d --start-address="$((pc-24))" --stop-address="$((pc+16))" "$module" || true
        fi
    fi
    exit "$status"
} 2>&1 | tee "$log"
