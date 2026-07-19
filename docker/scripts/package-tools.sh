#!/usr/bin/env bash
# ==============================================================================
# package-tools.sh - package the tool-family outputs into release assets.
#
# The single implementation of tools packaging: both the final image's
# artifact stage and the CI extraction path (make artifact-tools) call this,
# so the two can never drift.
#
# Inputs: a bin/tools tree (all families) and the wheels dir produced by the
# python family's build. Validates both -- an empty tree or a wheel count
# other than one means a broken tools build, and a release must fail loudly.
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

wheel_count=$(find "$WHEELS" -maxdepth 1 -name '*.whl' 2>/dev/null | wc -l)
if [ "$wheel_count" -ne 1 ]; then
  echo "[package-tools] ERROR: expected exactly 1 wheel in $WHEELS, found $wheel_count" >&2
  exit 1
fi

mkdir -p "$OUT"
tar -czf "$OUT/apex-tools-${VERSION}-x86_64-linux.tar.gz" \
  -C "$(dirname "$TOOLS_BIN")" "$(basename "$TOOLS_BIN")"
cp "$(find "$WHEELS" -maxdepth 1 -name '*.whl')" \
  "$OUT/apex_py_tools-${VERSION}-py3-none-any.whl"
echo "[package-tools] apex-tools-${VERSION}-x86_64-linux.tar.gz + wheel -> $OUT"
