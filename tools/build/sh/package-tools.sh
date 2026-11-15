#!/usr/bin/env bash
# ==============================================================================
# package-tools.sh - package the tools' outputs into release assets.
#
# The single implementation of tools packaging: `make artifact-tools` calls
# it for the local path and for the release, so the two can never drift.
#
# Inputs: a bin/tools tree (every role) and the wheels dir the python build
# writes, one subdir per role with one wheel per member. Validates both -- an
# empty tree or no wheel means a broken tools build, and a release must fail
# loudly.
#
# Usage: package-tools.sh <tools-bin-dir> <wheels-dir> <out-dir> <version>
# ==============================================================================
set -euo pipefail

TOOLS_BIN="${1:?usage: package-tools.sh <tools-bin-dir> <wheels-dir> <out-dir> <version>}"
WHEELS="${2:?usage: package-tools.sh <tools-bin-dir> <wheels-dir> <out-dir> <version>}"
OUT="${3:?usage: package-tools.sh <tools-bin-dir> <wheels-dir> <out-dir> <version>}"
VERSION="${4:?usage: package-tools.sh <tools-bin-dir> <wheels-dir> <out-dir> <version>}"

if [ -z "$(find "$TOOLS_BIN" -type f 2>/dev/null | head -1)" ]; then
  echo "[package-tools] ERROR: no files under $TOOLS_BIN (tools build broken)" >&2
  exit 1
fi

# The wheels are listed once, before anything is written, and the copy below
# works from that list. A wheels dir that does not exist has no wheel; a tree
# that cannot be read in full is an error of its own, because a partial list
# would ship a partial release.
if [ ! -d "$WHEELS" ]; then
  echo "[package-tools] ERROR: no wheel under $WHEELS (python tools build broken)" >&2
  exit 1
fi
if ! wheel_list=$(find "$WHEELS" -name '*.whl'); then
  echo "[package-tools] ERROR: cannot read every directory under $WHEELS" >&2
  exit 1
fi
if [ -z "$wheel_list" ]; then
  echo "[package-tools] ERROR: no wheel under $WHEELS (python tools build broken)" >&2
  exit 1
fi
wheel_count=$(printf '%s\n' "$wheel_list" | wc -l)

mkdir -p "$OUT"
tar -czf "$OUT/apex-tools-${VERSION}-x86_64-linux.tar.gz" \
  -C "$(dirname "$TOOLS_BIN")" "$(basename "$TOOLS_BIN")"
# Every wheel copies or the packager fails: a copy error is a broken release
# asset, never a skipped file.
copied=0
while IFS= read -r wheel; do
  cp -- "$wheel" "$OUT/"
  copied=$((copied + 1))
done <<<"$wheel_list"
if [ "$copied" -ne "$wheel_count" ]; then
  echo "[package-tools] ERROR: copied $copied of $wheel_count wheels" >&2
  exit 1
fi
echo "[package-tools] apex-tools-${VERSION}-x86_64-linux.tar.gz + ${wheel_count} wheel(s) -> $OUT"
