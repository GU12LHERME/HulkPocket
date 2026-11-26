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
    seeds=$(python tools/discover_hulk_xpp.py "$xbe" "$base")
    if [[ -n "$seeds" ]]; then
        recompiler="${HULK_RECOMPILER:-}"
        if [[ -z "$recompiler" ]]; then
            mapfile -t candidates < <(find "$HOME" -maxdepth 6 -type f -name xbe_recomp_host -executable 2>/dev/null | sort)
            if [[ "${#candidates[@]}" -ne 1 ]]; then
                printf 'Expected one xbe_recomp_host; candidates:\n%s\n' "${candidates[*]:-none}"
                echo 'Set HULK_RECOMPILER to the executable path and repeat.'
                exit 2
            fi
            recompiler="${candidates[0]}"
        fi
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
