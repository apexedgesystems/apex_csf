#!/usr/bin/env bash
# ==============================================================================
# check.sh - the apex_add_bundle contract, asserted from the outside
#
# Drives the fixture beside this script through the real packaging module and
# checks the user's view of a bundle: it packages from a tree where nothing is
# built yet, it carries each listed tool and only what that tool needs, every
# tool runs from a copy of the package somewhere else, and whatever cannot work
# on the target fails the configure or the package step.
#
# Usage: check.sh            (run by `make check-bundle-packaging`)
# ==============================================================================

set -u

fixture=$(cd "$(dirname "$0")" && pwd)
scratch=$(mktemp -d) || exit 1
trap 'rm -rf "$scratch"' EXIT
fail=0

note() {
  printf '[check-bundle-packaging] %s\n' "$*"
  fail=1
}

# Show the end of a log when a case goes wrong, so the cause is in the output.
show() {
  [ -f "$1" ] && tail -n 15 "$1" | sed 's/^/    | /'
}

configure() {
  local name=$1
  shift
  cmake -S "$fixture" -B "$scratch/$name" -G Ninja "$@" >"$scratch/$name.cfg.log" 2>&1
}

package() {
  cmake --build "$scratch/$1" --target package_FxBundle >"$scratch/$1.pkg.log" 2>&1
}

# A configure that must fail, with each given text in its output. CMake wraps
# its messages, so each text is a single word or token.
expect_configure_error() {
  local name=$1
  shift
  if configure "$name" -DCASE="$name"; then
    note "$name: expected a configure error"
    return
  fi
  local text
  for text in "$@"; do
    grep -q -- "$text" "$scratch/$name.cfg.log" || {
      note "$name: missing diagnostic \"$text\""
      show "$scratch/$name.cfg.log"
    }
  done
}

# A package step that must fail, with each given text in its output.
expect_package_error() {
  local name=$1
  shift
  if package "$name"; then
    note "$name: expected the package step to fail"
    return
  fi
  local text
  for text in "$@"; do
    grep -q -- "$text" "$scratch/$name.pkg.log" || {
      note "$name: missing diagnostic \"$text\""
      show "$scratch/$name.pkg.log"
    }
  done
}

# An ELF header for an architecture this machine is not: sixteen identification
# bytes, the file type, then the machine field.
foreign_elf() {
  {
    printf '\177ELF\002\001\001\000\000\000\000\000\000\000\000\000\002\000'
    if [ "$(uname -m)" = "riscv64" ]; then
      printf '\267\000' # aarch64
    else
      printf '\363\000' # riscv
    fi
  } >"$1"
}

# ------------------------------------------------------------------------------
# good: package from an unbuilt tree, then run everything from a copy elsewhere
# ------------------------------------------------------------------------------

if ! configure good -DCASE=good; then
  note "good: configure failed"
  show "$scratch/good.cfg.log"
elif ! package good; then
  note "good: packaging from a tree where nothing is built failed"
  show "$scratch/good.pkg.log"
else
  pkg="$scratch/good/packages/FxBundle"
  for f in FxApp/bank_a/bin/fx_exec FxApp/bank_a/tprm/master.tprm FxApp/run.sh \
    tools/bin/fx_cpp_tool tools/bin/fx-bin tools/bin/fx-script.sh tools/bin/fx-py \
    tools/lib/python/fxpkg/__init__.py tools/lib/python/fxdep/__init__.py \
    tools/REQUIREMENTS.txt; do
    [ -e "$pkg/$f" ] || note "good: the package has no $f"
  done
  ls "$pkg"/tools/libs/libfx_dep.so.1* >/dev/null 2>&1 ||
    note "good: the C++ tool's shared library is not in tools/libs"
  [ ! -e "$pkg/tools/lib/python/unrelated" ] ||
    note "good: the package carries a Python distribution no listed tool requires"
  [ ! -e "$pkg/tools/lib/python/bin" ] ||
    note "good: the package carries the build tree's console scripts"
  rows=$(grep -c '^fx' "$pkg/tools/REQUIREMENTS.txt" 2>/dev/null)
  [ "$rows" = "4" ] || note "good: REQUIREMENTS.txt lists $rows tools, expected 4"
  for kind in cpp bin script py; do
    grep -q "tool .* ($kind) staged" "$scratch/good.pkg.log" ||
      note "good: the package step printed no size line for the $kind tool"
  done

  # The user's copy: unpack the tarball somewhere else and take the build tree
  # away, so nothing can resolve back into it.
  mkdir "$scratch/elsewhere" &&
    tar -xzf "$scratch/good/packages/FxBundle.tar.gz" -C "$scratch/elsewhere" &&
    mv "$scratch/good" "$scratch/good.gone"
  moved="$scratch/elsewhere/FxBundle"
  run() { (cd / && env -i PATH=/usr/bin:/bin "$moved/$1" 2>&1); }
  [ "$(run FxApp/bank_a/bin/fx_exec)" = "fx_exec" ] || note "good: the executive does not run from the copy"
  [ "$(run tools/bin/fx_cpp_tool)" = "fx_cpp_tool with fx_dep" ] ||
    note "good: the C++ tool does not run from the copy: $(run tools/bin/fx_cpp_tool)"
  [ "$(run tools/bin/fx-bin)" = "fx-bin" ] || note "good: the prebuilt tool does not run from the copy"
  [ "$(run tools/bin/fx-script.sh)" = "fx-script" ] || note "good: the shell tool does not run from the copy"
  [ "$(run tools/bin/fx-py)" = "fx-py with fxdep as fx-py" ] ||
    note "good: the Python tool does not run from the copy: $(run tools/bin/fx-py)"

  # A target without python3 gets a plain message, not a traceback.
  mkdir "$scratch/empty-path"
  out=$(env -i PATH="$scratch/empty-path" "$moved/tools/bin/fx-py" 2>&1)
  rc=$?
  { [ "$rc" -eq 127 ] && printf '%s' "$out" | grep -q 'no python3 was found'; } ||
    note "good: without python3 the Python tool exits $rc and says: $out"
fi

# ------------------------------------------------------------------------------
# Tools built for another architecture fail the package step
# ------------------------------------------------------------------------------

foreign_elf "$scratch/foreign-tool"
if configure foreign-bin -DCASE=good -DFX_BIN_FILE="$scratch/foreign-tool"; then
  expect_package_error foreign-bin 'tool fx-bin cannot be staged' 'is built for' 'and the package is'
else
  note "foreign-bin: configure failed"
  show "$scratch/foreign-bin.cfg.log"
fi

if configure foreign-py -DCASE=good; then
  foreign_elf "$scratch/foreign-py/pylib/fxdep/_native.so" &&
    printf 'fxdep/_native.so,,\n' >>"$scratch/foreign-py/pylib/fxdep-2.0.dist-info/RECORD"
  expect_package_error foreign-py 'tool fx-py cannot be staged' '_native.so' 'and the package is'
else
  note "foreign-py: configure failed"
  show "$scratch/foreign-py.cfg.log"
fi

# ------------------------------------------------------------------------------
# Names and builds a bundle refuses at configure
# ------------------------------------------------------------------------------

expect_configure_error unknown-tool 'apex_add_bundle(FxBundle)' 'no-such-tool' 'provides'
expect_configure_error library-as-tool 'apex_add_bundle(FxBundle)' 'fx_dep' 'SHARED_LIBRARY'
expect_configure_error baremetal-tools 'apex_add_bundle(FxBundle)' 'bare-metal' 'filesystem'

if ! configure baremetal-plain -DCASE=baremetal-plain; then
  note "baremetal-plain: a bundle without tools must still configure in a bare-metal build"
  show "$scratch/baremetal-plain.cfg.log"
fi

[ "$fail" -eq 0 ] || exit 1
printf '[check-bundle-packaging] OK -- packages from an unbuilt tree; one tool of each kind runs from a copy elsewhere; foreign-architecture tools fail the package step; unknown, library and bare-metal tools fail the configure\n'
