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

if [[ ! -d "$SAMPLES_DIR" ]]; then
  echo "error: samples directory not found: $SAMPLES_DIR" >&2
  exit 1
fi

LOG_DIR="$(mktemp -d "${TMPDIR:-/tmp}/quartz_sanity_XXXXXX")"
cleanup() {
  echo "Logs written to: $LOG_DIR" >&2
}
trap cleanup EXIT

run_step() {
  local title="$1"
  local log_file="$2"
  shift 2

  echo "  - $title"
  if ! "$@" >"$log_file" 2>&1; then
    echo "    FAILED: $title" >&2
    echo "    Command: $*" >&2
    echo "    --- tail(80) $log_file ---" >&2
    tail -80 "$log_file" >&2 || true
    return 1
  fi
}

run_step_pipe_input() {
  local title="$1"
  local log_file="$2"
  shift 2

  echo "  - $title"
  if ! { printf '%s\n' "$SANITY_INPUT" | "$@"; } >"$log_file" 2>&1; then
    echo "    FAILED: $title" >&2
    echo "    Command: printf '%s\\n' \"$SANITY_INPUT\" | $*" >&2
    echo "    --- tail(80) $log_file ---" >&2
    tail -80 "$log_file" >&2 || true
    return 1
  fi
}

echo "[1/3] Removing all .qzb under samples/ ..."
find "$SAMPLES_DIR" -type f -name '*.qzb' -print0 | xargs -0 rm -f --

echo "[2/3] Discovering .qz files under samples/ ..."
TOTAL=$(find "$SAMPLES_DIR" -type f -name '*.qz' | wc -l | tr -d '[:space:]')

if [[ "$TOTAL" == "0" ]]; then
  echo "error: no .qz files found under $SAMPLES_DIR" >&2
  exit 1
fi

echo "[3/3] Running sanity checks (interpreter + compile + run-bc) ..."
FAILURES=0

while IFS= read -r src; do
  rel="${src#"$ROOT_DIR/"}"
  out="${src%.qz}.qzb"

  safe_name="$(echo "$rel" | tr '/ ' '__')"
  interp_log="$LOG_DIR/${safe_name}.interp.log"
  compile_log="$LOG_DIR/${safe_name}.compile.log"
  bc_log="$LOG_DIR/${safe_name}.bc.log"

  echo "==> $rel"

  if ! run_step_pipe_input "interp" "$interp_log" "$QUARTZ_BIN" --interp "$src"; then
    FAILURES=$((FAILURES + 1))
    continue
  fi

  if ! run_step "compile" "$compile_log" "$QUARTZ_BIN" --compile "$src" -o "$out"; then
    FAILURES=$((FAILURES + 1))
    continue
  fi

  if ! run_step_pipe_input "run-bc" "$bc_log" "$QUARTZ_BIN" --run-bc "$out"; then
    FAILURES=$((FAILURES + 1))
    continue
  fi

done < <(find "$SAMPLES_DIR" -type f -name '*.qz' | sort)

echo ""
echo "Sanity check complete: $((TOTAL - FAILURES))/$TOTAL passed"

if [[ $FAILURES -ne 0 ]]; then
  exit 1
fi
