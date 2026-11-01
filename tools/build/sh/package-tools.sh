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
find "$WHEELS" -name '*.whl' -exec cp {} "$OUT/" \;
echo "[package-tools] apex-tools-${VERSION}-x86_64-linux.tar.gz + ${wheel_count} wheel(s) -> $OUT"
