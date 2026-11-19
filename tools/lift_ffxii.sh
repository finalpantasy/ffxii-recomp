#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LIFTER="${FFXII_LIFTER:-$ROOT/build/swarc/tools/lifter_x86/recomp_lift}"
SYMBOLS="${FFXII_SYMBOLS:-$ROOT/analysis/maps}"
OUT="${1:-$ROOT/generated/full}"
shift || true

: "${FFXII_EXE:?Set FFXII_EXE to the local FFXII_TZA.exe path}"
if [[ ! -x "$LIFTER" ]]; then
    printf 'SwARc lifter not found or not executable: %s\n' "$LIFTER" >&2
    exit 1
fi
for file in functions.tsv jumptables.tsv exports.tsv lifter.manifest; do
    if [[ ! -f "$SYMBOLS/$file" ]]; then
        printf 'Missing lifter input: %s\n' "$SYMBOLS/$file" >&2
        exit 1
    fi
done

exec "$LIFTER" --exe "$FFXII_EXE" --symbols "$SYMBOLS" --manifest "$SYMBOLS/lifter.manifest" --out "$OUT" "$@"
