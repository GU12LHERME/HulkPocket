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
            -ldl -lEGL -lGLESv3 -landroid -o "$out"
    fi
    args=()
    if [[ -n "$disc" ]]; then args+=(--disc-root "$disc"); fi
    if [[ -n "${HULK_PARTITION1_ROOT:-}" ]]; then args+=(--partition1-root "$HULK_PARTITION1_ROOT"); fi
    execution_log=$(mktemp)
    trap 'rm -f "$execution_log"' EXIT
    # Local opt-in output; never upload original assets or replay pixels.
    export HULK_REPLAY_BITMAP="${HULK_REPLAY_BITMAP:-$(dirname "$log")/HulkPocket-replay-$(date +%Y%m%d-%H%M%S).bmp}"
    if [[ "${HULK_COMPACT_REPORT:-0}" != 1 || "${HULK_CAPTURE_PCM:-0}" == 1 ]]; then
        export HULK_AUDIO_PCM="${HULK_AUDIO_PCM:-$(dirname "$log")/HulkPocket-audio-$(date +%Y%m%d-%H%M%S).pcm}"
    fi
    status=0
    if [[ "${HULK_COMPACT_REPORT:-0}" == 1 ]]; then
        "$out" "$xbe" "$module" --execute-diagnostic "${args[@]}" 2>&1 | python tools/compact_hulk_report.py >"$execution_log" || {
            statuses=("${PIPESTATUS[@]}")
            status=${statuses[0]}
            if (( statuses[1] != 0 )); then echo "Compact diagnostic filter failed"; exit 2; fi
        }
    else
        "$out" "$xbe" "$module" --execute-diagnostic "${args[@]}" >"$execution_log" 2>&1 || status=$?
    fi
    cat "$execution_log"
    if grep -q "D3D original viewport has zero extent" "$execution_log"; then
        python tools/report_hulk_viewport.py "$generated" "$execution_log" || true
    fi
    if grep -q "Unsupported dispatcher pointer wait" "$execution_log"; then
        python - "$generated" "$execution_log" <<'PY' || true
import pathlib,re,sys
sys.path.insert(0,"tools")
from report_hulk_viewport import caller_context
log=pathlib.Path(sys.argv[2]).read_text(errors="replace")
callers={int(x,16) for x in re.findall(r"^KERNEL POINTER WAIT caller=([0-9A-Fa-f]{8})",log,re.M)}
print("KERNEL WAIT ORIGINAL CALLER CONTEXT")
caller_context(pathlib.Path(sys.argv[1]),set(sorted(callers)[:16]))
PY
    fi
    if grep -q "MISSING GUEST target=" "$execution_log"; then
        python tools/report_missing_guest.py "$xbe" "$generated" --log "$execution_log" || true
    fi
    if grep -q "HULK ALLOC caller=" "$execution_log"; then
        python tools/report_hulk_allocator.py "$generated" "$execution_log" || true
    fi
    pc=$(sed -n 's/.*pc_minus_module_base=\(0x[0-9A-Fa-f]*\).*/\1/p' "$execution_log" | tail -n 1)
    if [[ -n "$pc" && -x "$PREFIX/bin/llvm-addr2line" ]]; then
        echo "Fault native function:"
        fault_symbol=$("$PREFIX/bin/llvm-addr2line" -f -C -e "$module" "$pc" | head -n 1) || fault_symbol=""
        printf '%s\n' "$fault_symbol"
        if [[ "$fault_symbol" == *sub_0038A2C0* ]]; then
            python tools/report_hulk_command_context.py "$generated" || true
        fi
        if [[ -x "$PREFIX/bin/llvm-objdump" ]]; then
            echo "Native instruction window:"
            "$PREFIX/bin/llvm-objdump" -d --start-address="$((pc-24))" --stop-address="$((pc+16))" "$module" || true
        fi
    fi
    exit "$status"
} 2>&1 | if [[ "${HULK_SINGLE_REPORT:-0}" == 1 ]]; then cat; else tee "$log"; fi
