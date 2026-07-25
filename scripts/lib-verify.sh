#!/usr/bin/env bash
# ==============================================================================
# lib-verify.sh - verify one library against its lib.manifest support contract.
#
# Finds the lib.manifest whose `name` matches the argument, then compiles the
# library's probe on every declared platform through the existing docker
# compose services: hosted probes once per declared posix_cpp dialect, MCU
# probes under each declared platform's real cross toolchain. Probe targets
# compile -Werror, so "verified" means no errors and no warnings.
#
# Usage: lib-verify.sh <target-name>          (e.g. utilities_math_vecmat)
# ==============================================================================
set -uo pipefail

lib="${1:?usage: lib-verify.sh <target-name>}"

repo="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo"

manifest="$(grep -rl --include=lib.manifest "^name ${lib}\$" src || true)"
if [ -z "$manifest" ]; then
  echo "lib-verify: no lib.manifest with 'name ${lib}'" >&2
  exit 2
fi

posix_cpp="$(awk '$1=="posix_cpp"{$1="";print}' "$manifest")"
mcu="$(awk '$1=="mcu"{$1="";print}' "$manifest")"

# Registry lookup: manifest mcu entries are APEX_HAL_PLATFORM names; the
# platform registry keys the avr toolchain under "arduino".
reg_name() {
  case "$1" in
  avr) echo arduino ;;
  *) echo "$1" ;;
  esac
}
reg_field() {
  sed -n "s/^P_$1_$2[[:space:]]*:=[[:space:]]*//p" mk/platforms.mk
}

export HOST_UID="${HOST_UID:-$(id -u)}"
export HOST_GID="${HOST_GID:-$(id -g)}"

fail=0
report=""

run() { # label service build_dir target [configure_preset]
  local label="$1" service="$2" dir="$3" target="$4" preset="${5:-}"
  local cfg=""
  [ -n "$preset" ] && cfg="[ -f ${dir}/CMakeCache.txt ] || cmake --preset ${preset} >/dev/null 2>&1; "
  if docker compose run --rm "$service" bash -lc \
    "${cfg}cmake --build ${dir} --target ${target}" >/tmp/lib-verify-$$.log 2>&1; then
    report="${report}  PASS  ${label}\n"
  else
    report="${report}  FAIL  ${label}\n"
    sed -n '1,25p' /tmp/lib-verify-$$.log
    fail=1
  fi
  rm -f /tmp/lib-verify-$$.log
}

echo "lib-verify: ${lib} (${manifest})"

for std in $posix_cpp; do
  run "posix c++${std}" dev-cuda build/hosted-x86_64-debug "${lib}_probe_cpp${std}"
done

for hal in $mcu; do
  p="$(reg_name "$hal")"
  preset="$(reg_field "$p" PRESET)"
  service="$(reg_field "$p" SERVICE)"
  if [ -z "$preset" ] || [ -z "$service" ]; then
    echo "lib-verify: platform '${hal}' not in mk/platforms.mk registry" >&2
    fail=1
    continue
  fi
  run "mcu ${hal}" "$service" "build/${preset}" "${lib}_probe" "$preset"
done

printf "%b" "$report"
[ "$fail" -eq 0 ] && echo "lib-verify: ${lib} OK" || echo "lib-verify: ${lib} FAILED"
exit "$fail"
