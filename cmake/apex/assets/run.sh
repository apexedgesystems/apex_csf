#!/bin/bash
# Apex deployment launcher
#
# Runs the single executive of an apex deployment. A deployment directory holds
# bank_a/{bin,libs,tprm} + this script, and that directory is the executive's
# filesystem root (where it reads its TPRM and writes system.log, the banks, and
# telemetry). Exactly one executive owns a deployment -- that is the safety
# invariant, so this script resolves the one executable under the active bank.
#
# Binaries self-locate their shared libraries via a $ORIGIN/../libs RPATH, so no
# LD_LIBRARY_PATH is needed and a supervisor in one deployment can launch an
# executive in another.
#
#   Direct:      ./run.sh [args...]
#                  runs this deployment's executive, this directory as fs-root
#
#   Supervised:  ./run.sh [supervisor-args...] -- <target-deployment> [child-args...]
#                  this deployment's executive (a supervisor, e.g. ApexWatchdog)
#                  launches the target deployment's executive across filesystems
#                  e.g.  ./run.sh --max-crashes 5 -- ../ApexHilDemo --shutdown-after 30
#
# Mode is chosen by the presence of `--`.

set -euo pipefail

# Active bank directory for a deployment dir (defaults to bank_a on first boot).
_bank_dir() {
  local dir="$1" bank="a"
  if [[ -f "$dir/active_bank" ]]; then
    bank="$(head -c1 "$dir/active_bank")"
  fi
  printf '%s/bank_%s' "$dir" "$bank"
}

# The single executable under a bank's bin/ (errors on zero or more than one).
_the_exec() {
  local bank="$1" found="" f
  for f in "$bank/bin/"*; do
    [[ -f "$f" && -x "$f" ]] || continue
    if [[ -n "$found" ]]; then
      echo "error: more than one executable in $bank/bin (a deployment has exactly one)" >&2
      return 1
    fi
    found="$f"
  done
  if [[ -z "$found" ]]; then
    echo "error: no executable in $bank/bin" >&2
    return 1
  fi
  printf '%s' "$found"
}

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LOCAL_BANK="$(_bank_dir "$SCRIPT_DIR")"
LOCAL_EXEC="$(_the_exec "$LOCAL_BANK")"

# Split args on the first `--`.
SUP_ARGS=()
CHILD=()
HAS_SEP=false
for arg in "$@"; do
  if ! $HAS_SEP && [[ "$arg" == "--" ]]; then
    HAS_SEP=true
    continue
  fi
  if $HAS_SEP; then CHILD+=("$arg"); else SUP_ARGS+=("$arg"); fi
done

cd "$SCRIPT_DIR"

if $HAS_SEP; then
  # Supervised: LOCAL_EXEC is a supervisor; the first token after `--` is the
  # target deployment directory, the rest are the child's args.
  if [[ ${#CHILD[@]} -lt 1 ]]; then
    echo "error: supervised mode needs a target deployment after '--'" >&2
    exit 1
  fi
  TARGET_DIR="$(cd "${CHILD[0]}" && pwd)"
  TARGET_BANK="$(_bank_dir "$TARGET_DIR")"
  TARGET_EXEC="$(_the_exec "$TARGET_BANK")"

  child_args=(--fs-root "$TARGET_DIR")
  if [[ -f "$TARGET_BANK/tprm/master.tprm" ]]; then
    child_args+=(--config "$TARGET_BANK/tprm/master.tprm")
  fi
  exec "$LOCAL_EXEC" "${SUP_ARGS[@]}" -- "$TARGET_EXEC" "${child_args[@]}" "${CHILD[@]:1}"
fi

# Direct: run this deployment's executive with this directory as its fs-root.
direct_args=(--fs-root "$SCRIPT_DIR")
if [[ -f "$LOCAL_BANK/tprm/master.tprm" ]]; then
  direct_args+=(--config "$LOCAL_BANK/tprm/master.tprm")
fi
exec "$LOCAL_EXEC" "${direct_args[@]}" "$@"
