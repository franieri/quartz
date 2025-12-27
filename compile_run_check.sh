#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QUARTZ_BIN="$ROOT_DIR/build/quartz"
SAMPLES_DIR="$ROOT_DIR/samples"

SANITY_INPUT="${SANITY_INPUT:-test input}"

if [[ ! -x "$QUARTZ_BIN" ]]; then
  echo "error: Quartz binary not found or not executable: $QUARTZ_BIN" >&2
  echo "hint: run: bash rebuild_core.sh" >&2
  exit 1
fi

LOG_DIR="$(mktemp -d "${TMPDIR:-/tmp}/quartz_compile_run_XXXXXX")"
cleanup() {
  echo "Logs written to: $LOG_DIR" >&2
}
trap cleanup EXIT

echo "[1/3] Removing all .qzb under samples/ ..."
find "$SAMPLES_DIR" -type f -name '*.qzb' -print0 | xargs -0 rm -f --

echo "[2/3] Running --compile-run for every .qz under samples/ ..."
FAILURES=0
TOTAL=$(find "$SAMPLES_DIR" -type f -name '*.qz' | wc -l | tr -d '[:space:]')

while IFS= read -r src; do
  rel="${src#"$ROOT_DIR/"}"
  out="${src%.qz}.qzb"
  safe_name="$(echo "$rel" | tr '/ ' '__')"
  log_file="$LOG_DIR/${safe_name}.compile_run.log"

  echo "==> $rel"
  if ! { printf '%s\n' "$SANITY_INPUT" | "$QUARTZ_BIN" --compile-run "$src" -o "$out"; } >"$log_file" 2>&1; then
    echo "  FAILED: --compile-run $rel" >&2
    echo "  --- tail(80) $log_file ---" >&2
    tail -80 "$log_file" >&2 || true
    FAILURES=$((FAILURES + 1))
  fi

done < <(find "$SAMPLES_DIR" -type f -name '*.qz' | sort)

echo ""
echo "[3/3] Summary"
echo "compile-run check: $((TOTAL - FAILURES))/$TOTAL passed"

if [[ $FAILURES -ne 0 ]]; then
  exit 1
fi
