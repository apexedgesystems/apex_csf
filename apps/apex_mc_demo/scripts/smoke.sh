#!/bin/bash
# ==============================================================================
# smoke.sh - Monte Carlo demo end-to-end smoke test
#
# Runs ApexMcDemo and asserts every Monte Carlo iteration completed (the
# summary's count column equals the run count). This exercises the thread-pool
# dispatch and clean-shutdown path: a lost wakeup strands a task or hangs the
# pool, surfacing here as a short count or a timeout even when unit tests pass.
#
# Invoked by the build system (make smoke), which passes APEX_BIN_DIR. All
# app-specific knowledge (CLI flags, summary format) lives here, not in the
# build system.
#
# Usage:
#   APEX_BIN_DIR=<bin> [SMOKE_RUNS=N] smoke.sh
#
# Environment:
#   APEX_BIN_DIR   Directory containing the built ApexMcDemo binary (required)
#   SMOKE_RUNS     Monte Carlo iteration count (default: 10000)
#
# Output:
#   One-line OK on success; non-zero exit with detail on failure.
#
# ==============================================================================

set -euo pipefail

readonly SCRIPT_NAME="smoke"

log_ok() { printf '\033[32m[%s]\033[0m %s\n' "$SCRIPT_NAME" "$1" >&2; }
err() { printf '\033[31m[%s]\033[0m %s\n' "$SCRIPT_NAME" "$1" >&2; }

# ==============================================================================
# Run and verify
# ==============================================================================

readonly BIN_DIR="${APEX_BIN_DIR:?APEX_BIN_DIR must be set (path to built binaries)}"
readonly RUNS="${SMOKE_RUNS:-10000}"
readonly DEMO="${BIN_DIR}/ApexMcDemo"

if [[ ! -x "$DEMO" ]]; then
  err "ApexMcDemo not found at $DEMO"
  exit 1
fi

SUMMARY="$(mktemp)"
readonly SUMMARY
trap 'rm -f "$SUMMARY"' EXIT

"$DEMO" --runs "$RUNS" --seed 42 --summary "$SUMMARY"

if [[ ! -s "$SUMMARY" ]]; then
  err "no summary produced"
  exit 1
fi

# Summary columns: field,count,...  Every field's count must equal the run count
# (otherwise a task was dropped or the pool stalled mid-run).
SHORT="$(awk -F, -v want="$RUNS" 'NR > 1 && $2 != want { print "  " $1 " count=" $2 }' "$SUMMARY")"
if [[ -n "$SHORT" ]]; then
  err "not all $RUNS iterations completed:"
  printf '%s\n' "$SHORT" >&2
  exit 1
fi

log_ok "ApexMcDemo OK -- all $RUNS iterations completed across all fields"
