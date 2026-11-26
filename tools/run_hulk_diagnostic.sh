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
    python tools/link_hulk_cached.py --sdk "$sdk" --generated "$generated" \
        --cache "$cache" --ndk-bin "$PREFIX/bin" --output "$module"
    clang++ -std=c++17 -O2 -pthread -DHULK_ENABLE_ENTRY_DIAGNOSTIC \
        -I"$sdk/runtime" native/hulk_xbe_local_diagnostic.cpp \
        native/hulk_xbe_preflight.cpp native/xbox_guest_address_space.cpp \
        native/xbe_image_memory_loader.cpp native/xbe_kernel_imports.cpp \
        native/portable_xbe_image.cpp native/portable_xbe_dispatch.cpp \
        -ldl -o "$out"
    args=()
    if [[ -n "$disc" ]]; then args+=(--disc-root "$disc"); fi
    execution_log=$(mktemp)
    trap 'rm -f "$execution_log"' EXIT
    status=0
    "$out" "$xbe" "$module" --execute-diagnostic "${args[@]}" >"$execution_log" 2>&1 || status=$?
    cat "$execution_log"
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
