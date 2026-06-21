#!/bin/bash
# Apex deployable launch script
#
# Reads the active bank marker, sets LD_LIBRARY_PATH to the active bank's
# libs/ directory, and runs the specified binary with the active bank's
# master.tprm config. The deployment directory IS the filesystem root.
#
# Direct mode:
#   ./run.sh <executive> [extra-args...]
#   sudo ./run.sh ApexHilDemo --skip-cleanup
#
# Watchdog mode:
#   ./run.sh ApexWatchdog [watchdog-args...] -- <executive> [extra-args...]
#   sudo ./run.sh ApexWatchdog --max-crashes 5 -- ApexHilDemo --skip-cleanup
#
# In watchdog mode, --fs-root and --config are injected into the executive
# args (after --), not into the watchdog args.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Read active bank marker (default: "a" on first boot)
ACTIVE_BANK="a"
if [[ -f "$SCRIPT_DIR/active_bank" ]]; then
  ACTIVE_BANK=$(head -c1 "$SCRIPT_DIR/active_bank")
fi
BANK_DIR="$SCRIPT_DIR/bank_${ACTIVE_BANK}"

if [[ $# -lt 1 ]]; then
  echo "Usage: ./run.sh <binary> [extra-args...]"
  echo "       ./run.sh ApexWatchdog [wd-args...] -- <executive> [extra-args...]"
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

# Watchdog mode: split args at --, resolve child binary, inject exec flags.
if [[ "$BIN_NAME" == "ApexWatchdog" ]]; then
  WD_ARGS=()
  CHILD_ARGS=()
  found_sep=false
  for arg in "$@"; do
    if [[ "$arg" == "--" ]]; then
      found_sep=true
      continue
    fi
    if $found_sep; then
      CHILD_ARGS+=("$arg")
    else
      WD_ARGS+=("$arg")
    fi
  done

  if ! $found_sep || [[ ${#CHILD_ARGS[@]} -lt 1 ]]; then
    echo "Watchdog mode requires: ApexWatchdog [wd-args...] -- <executive> [extra-args...]"
    exit 1
  fi

  # Resolve child executable name to bank path
  CHILD_NAME="${CHILD_ARGS[0]}"
  CHILD_BIN="$BANK_DIR/bin/$CHILD_NAME"
  if [[ ! -x "$CHILD_BIN" ]]; then
    echo "Not found: bank_${ACTIVE_BANK}/bin/$CHILD_NAME"
    exit 1
  fi

  # Replace child name with resolved path, inject --fs-root and --config
  exec "$APP_BIN" "${WD_ARGS[@]}" \
    -- "$CHILD_BIN" --fs-root . --config "$TPRM_CONFIG" "${CHILD_ARGS[@]:1}"
fi

# Direct mode: run binary with --fs-root and --config.
exec "$APP_BIN" --fs-root . --config "$TPRM_CONFIG" "$@"
