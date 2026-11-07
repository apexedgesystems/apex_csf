#!/usr/bin/env bash
# ==============================================================================
# package-tools.sh - package the tool-family outputs into release assets.
#
# The single implementation of tools packaging: both the final image's
# artifact stage and the CI extraction path (make artifact-tools) call this,
# so the two can never drift.
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

wheel_count=$(find "$WHEELS" -name '*.whl' 2>/dev/null | wc -l)
if [ "$wheel_count" -lt 1 ]; then
  echo "[package-tools] ERROR: no wheel under $WHEELS (python tools build broken)" >&2
  exit 1
fi

mkdir -p "$OUT"
tar -czf "$OUT/apex-tools-${VERSION}-x86_64-linux.tar.gz" \
  -C "$(dirname "$TOOLS_BIN")" "$(basename "$TOOLS_BIN")"
# Every wheel copies or the packager fails: a copy error is a broken release
# asset, never a skipped file.
copied=0
while IFS= read -r wheel; do
  cp -- "$wheel" "$OUT/"
  copied=$((copied + 1))
done < <(find "$WHEELS" -name '*.whl')
if [ "$copied" -ne "$wheel_count" ]; then
  echo "[package-tools] ERROR: copied $copied of $wheel_count wheels" >&2
  exit 1
fi
echo "[package-tools] apex-tools-${VERSION}-x86_64-linux.tar.gz + ${wheel_count} wheel(s) -> $OUT"
