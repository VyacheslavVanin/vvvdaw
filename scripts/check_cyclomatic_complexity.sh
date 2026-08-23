#!/usr/bin/env bash
# Check the whole project's cyclomatic complexity with pmccabe.
#
# Reports every function at or above the warn threshold and fails (exit 1) when
# a single function exceeds the hard max. Thresholds can be overridden with the
# VVDRAW_COMPLEXITY_WARN / VVDRAW_COMPLEXITY_MAX environment variables.
#
# If pmccabe is not installed the check is skipped with a hint (exit 0).
#
# pmccabe is run once per file: passing many translation units to a single
# invocation makes its brace counter overflow and emit bogus "too many }'s"
# errors, so each file is analyzed independently and results are aggregated.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WARN="${VVDRAW_COMPLEXITY_WARN:-10}"
MAX="${VVDRAW_COMPLEXITY_MAX:-40}"

if ! command -v pmccabe >/dev/null 2>&1; then
    echo "check_cyclomatic_complexity: pmccabe is not installed; skipping the check." >&2
    echo "  Install it with, e.g.:  sudo apt install pmccabe" >&2
    exit 0
fi

mapfile -d '' FILES < <(find "$ROOT/src" "$ROOT/tests" -name '*.cpp' -print0)
if ((${#FILES[@]} == 0)); then
    echo "check_cyclomatic_complexity: no .cpp files found under src/ and tests/." >&2
    exit 0
fi

echo "=== Cyclomatic complexity (pmccabe): warn >= $WARN, fail > $MAX ==="
TOTAL_FUNCS=0
MAX_COMPLEXITY=0
WORST=""
WARN_COUNT=0
OVER_MAX=0

for f in "${FILES[@]}"; do
    # Per-function pmccabe line: "<complexity> ... <path>(<line>): <name>".
    while IFS=$'\t' read -r comp func; do
        TOTAL_FUNCS=$((TOTAL_FUNCS + 1))
        if ((comp > MAX_COMPLEXITY)); then
            MAX_COMPLEXITY=$comp
            WORST="$func"
        fi
        if ((comp >= WARN)); then
            WARN_COUNT=$((WARN_COUNT + 1))
            printf '  %-70s complexity=%d\n' "$func" "$comp"
        fi
        if ((comp > MAX)); then
            OVER_MAX=$((OVER_MAX + 1))
        fi
    done < <(pmccabe -X "$f" 2>/dev/null | awk '
        $0 ~ /\(.*\): / {
            comp = $1 + 0
            sub(/^([^\t]*\t){5}/, "")
            print comp "\t" $0
        }
    ')
done

echo "Total functions: $TOTAL_FUNCS"
if ((MAX_COMPLEXITY > 0)); then
    echo "Max complexity: $MAX_COMPLEXITY ($WORST)"
fi
echo "Functions at/above warn($WARN): $WARN_COUNT"
if ((OVER_MAX > 0)); then
    echo "FAIL: $OVER_MAX function(s) exceed max($MAX)"
    exit 1
fi
echo "OK: no function exceeds max($MAX)"