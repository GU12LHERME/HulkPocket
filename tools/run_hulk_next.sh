#!/usr/bin/env bash
# Single private mobile validation: seed verified XPP callbacks, cache, run, report.
set -euo pipefail
cd "$(dirname "$0")/.."
sdk="$HOME/XboxXBERecomp"
base="$HOME/hulk-generated"
cache="$HOME/hulk-object-cache"
xbe="$HOME/default.xbe"
disc="${HULK_DISC_ROOT:-}"
downloads="$HOME/storage/downloads"
[[ -d "$downloads" ]] || downloads="$HOME"
log="$downloads/HulkPocket-$(date +%Y%m%d-%H%M%S).txt"
{
    echo "HulkPocket revision: $(git rev-parse --short HEAD)"
    python tools/audit_hulk_stream_vtables.py "$xbe" "$base" || echo 'DirectSound vtable audit inconclusive; no override enabled'
    seeds=$(python tools/discover_hulk_xpp.py "$xbe" "$base")
    if [[ -n "$seeds" ]]; then
        recompiler="${HULK_RECOMPILER:-}"
        if [[ -z "$recompiler" ]]; then
            recompiler="$HOME/hulk-recompiler-native/xbe_recomp_host"
            if [[ ! -x "$recompiler" ]]; then
                echo "Building native ARM64 recompiler tool (not the game)"
                for tool in cmake clang clang++ make git; do
                    if ! command -v "$tool" >/dev/null; then
                        echo "Missing $tool. In Termux run: pkg install cmake clang make git"
                        exit 2
                    fi
                done
                cmake -S tools/xbe-recompiler-host -B "$HOME/hulk-recompiler-native" \
                    -DXBE_SDK_DIR="$sdk" -DCMAKE_BUILD_TYPE=Release \
                    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
                cmake --build "$HOME/hulk-recompiler-native" --target xbe_recomp_host --parallel 2
            fi
        fi
        if [[ ! -x "$recompiler" ]]; then
            echo "Recompiler not executable: $recompiler"
            exit 2
        fi
        echo "Native recompiler: $recompiler"
        args=()
        while IFS= read -r seed; do args+=(--seed "$seed"); done <<<"$seeds"
        key=$(printf '%s' "$seeds" | sha256sum | cut -c1-12)
        output="$HOME/hulk-generated-xpp-$key"
        echo "Verified XPP callback seeds: $seeds"
        python tools/extend_hulk_generated.py "${args[@]}" --recompiler "$recompiler" --base "$base" --output "$output"
        base="$output"
    fi
    bash tools/run_hulk_diagnostic.sh "$sdk" "$base" "$cache" "$xbe" "$disc"
} 2>&1 | tee "$log"
