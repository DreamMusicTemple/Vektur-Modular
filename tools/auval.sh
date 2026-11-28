#!/bin/bash
# auval.sh — validates the Vektur Modular AUv3 on macOS (Phase 7 DAW matrix).
#
# Build and run the macOS app once first (it registers the extension), then:
#   tools/auval.sh            # strict validation, 3 passes
#   tools/auval.sh --stress   # + 50 load/unload cycles of auvaltool
# Exit status is non-zero on any failure. Logs go to build/auval/.
set -euo pipefail

TYPE=aumu SUBTYPE=VkMd MANU=DMTm     # must match AudioComponentInfo.swift
OUT="$(cd "$(dirname "$0")/.." && pwd)/build/auval"
mkdir -p "$OUT"

if ! command -v auval >/dev/null; then echo "auval not found (macOS only)"; exit 2; fi

echo "== Component registered?"
if ! auval -s "$TYPE" 2>/dev/null | grep -q "$SUBTYPE $MANU"; then
    echo "   not found — launch the macOS app once, or run: pluginkit -m -v -p com.apple.AudioUnit-UI | grep -i vektur"
    exit 1
fi

echo "== Strict validation (3 passes)"
for pass in 1 2 3; do
    if auval -strict -v "$TYPE" "$SUBTYPE" "$MANU" > "$OUT/strict-$pass.log" 2>&1; then
        echo "   pass $pass: OK"
    else
        echo "   pass $pass: FAILED — see $OUT/strict-$pass.log"; tail -30 "$OUT/strict-$pass.log"; exit 1
    fi
done
grep -E "AU VALIDATION SUCCEEDED|WARNING" "$OUT/strict-1.log" || true

if [[ "${1:-}" == "--stress" ]]; then
    echo "== Load / unload ×50 (auvaltool -r 50 opens, initialises, renders and closes the unit each time)"
    if auval -r 50 -v "$TYPE" "$SUBTYPE" "$MANU" > "$OUT/stress.log" 2>&1; then echo "   OK"
    else echo "   FAILED — see $OUT/stress.log"; tail -30 "$OUT/stress.log"; exit 1; fi
fi
echo "All auval checks passed. Logs: $OUT"
