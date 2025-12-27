#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QUARTZ_BIN="$ROOT_DIR/build/quartz"
SAMPLES_DIR="$ROOT_DIR/samples"

if [[ ! -x "$QUARTZ_BIN" ]]; then
  echo "error: Quartz binary not found or not executable: $QUARTZ_BIN" >&2
  echo "hint: run: bash rebuild_core.sh" >&2
  exit 1
fi

echo "[1/2] Running full sanity (interp + compile + run-bc) ..."
"$ROOT_DIR/sanity_check.sh"

echo "[2/2] Verifying bytecode metadata for all compiled samples ..."
FAILURES=0
TOTAL=$(find "$SAMPLES_DIR" -type f -name '*.qzb' | wc -l | tr -d '[:space:]')

if [[ "$TOTAL" == "0" ]]; then
  echo "error: sanity_check produced no .qzb files under samples/" >&2
  exit 1
fi

while IFS= read -r bc; do
  rel="${bc#"$ROOT_DIR/"}"
  if ! "$QUARTZ_BIN" --dump-qzb-meta "$bc" >/dev/null 2>&1; then
    echo "FAILED: --dump-qzb-meta $rel" >&2
    FAILURES=$((FAILURES + 1))
  fi

done < <(find "$SAMPLES_DIR" -type f -name '*.qzb' | sort)

echo ""
echo "Feature check complete: $((TOTAL - FAILURES))/$TOTAL metadata dumps succeeded"

if [[ $FAILURES -ne 0 ]]; then
  exit 1
fi
