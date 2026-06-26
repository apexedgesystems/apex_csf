#!/bin/bash
# Apex POSIX deployable launcher
#
# Generic launcher for an apex filesystem bundle: resolves the active bank, sets
# LD_LIBRARY_PATH to its libs/, and runs an executive with the active bank's
# master.tprm config, treating the deployment directory as the filesystem root.
#
# Works for any executive -- the two apex defaults (the ApexExecutive-derived
# posix executive and the ApexWatchdog supervisor) and custom ones built on the
# same infrastructure -- by convention, not by hardcoded names:
#
#   Direct:      ./run.sh <executive> [args...]
#                ./run.sh ApexHilDemo --skip-cleanup
#
#   Supervised:  ./run.sh <supervisor> [sup-args...] -- <executive> [args...]
#                ./run.sh ApexWatchdog --max-crashes 5 -- ApexHilDemo --skip-cleanup
#
# Mode is chosen by the presence of `--`: with it, the first binary is a
# supervisor and the apex flags (--fs-root/--config) are injected into the child
# (after `--`); without it, they are injected into the binary directly.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Read active bank marker (default: "a" on first boot)
ACTIVE_BANK="a"
if [[ -f "$SCRIPT_DIR/active_bank" ]]; then
  ACTIVE_BANK=$(head -c1 "$SCRIPT_DIR/active_bank")
fi
BANK_DIR="$SCRIPT_DIR/bank_${ACTIVE_BANK}"

if [[ $# -lt 1 ]]; then
  echo "Usage: ./run.sh <executive> [args...]"
  echo "       ./run.sh <supervisor> [sup-args...] -- <executive> [args...]"
  echo ""
  echo "Active bank: $ACTIVE_BANK"
  echo "Available:"
  for f in "$BANK_DIR/bin/"*; do
    if [[ -f "$f" && -x "$f" ]]; then echo "  $(basename "$f")"; fi
  done
  exit 1
fi

BIN_NAME="$1"
shift
APP_BIN="$BANK_DIR/bin/$BIN_NAME"

if [[ ! -x "$APP_BIN" ]]; then
  echo "Not found: bank_${ACTIVE_BANK}/bin/$BIN_NAME"
  exit 1
fi

export LD_LIBRARY_PATH="$BANK_DIR/libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

TPRM_CONFIG="$BANK_DIR/tprm/master.tprm"

cd "$SCRIPT_DIR"

# Supervised mode: a `--` separates the supervisor's own args from the child
# executive it launches. Inject the apex flags into the child, not the
# supervisor. Name-agnostic: works for ApexWatchdog or any custom supervisor.
HAS_SEP=false
for arg in "$@"; do
  if [[ "$arg" == "--" ]]; then
    HAS_SEP=true
    break
  fi
done

if $HAS_SEP; then
  SUP_ARGS=()
  CHILD_ARGS=()
  found_sep=false
  for arg in "$@"; do
    if [[ "$arg" == "--" ]] && ! $found_sep; then
      found_sep=true
      continue
    fi
    if $found_sep; then
      CHILD_ARGS+=("$arg")
    else
      SUP_ARGS+=("$arg")
    fi
  done

  if [[ ${#CHILD_ARGS[@]} -lt 1 ]]; then
    echo "Supervised mode requires a child executive after '--'"
    exit 1
  fi

  CHILD_NAME="${CHILD_ARGS[0]}"
  CHILD_BIN="$BANK_DIR/bin/$CHILD_NAME"
  if [[ ! -x "$CHILD_BIN" ]]; then
    echo "Not found: bank_${ACTIVE_BANK}/bin/$CHILD_NAME"
    exit 1
  fi

  # Resolve the child to its bank path and inject --fs-root/--config into it.
  exec "$APP_BIN" "${SUP_ARGS[@]}" \
    -- "$CHILD_BIN" --fs-root . --config "$TPRM_CONFIG" "${CHILD_ARGS[@]:1}"
fi

# Direct mode: run the executive with --fs-root and --config.
exec "$APP_BIN" --fs-root . --config "$TPRM_CONFIG" "$@"
