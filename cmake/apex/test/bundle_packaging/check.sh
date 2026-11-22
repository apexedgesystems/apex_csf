#!/usr/bin/env bash
# ==============================================================================
# check.sh - the apex_add_bundle contract, asserted from the outside
#
# Drives the fixture beside this script through the real packaging module and
# checks the user's view of a bundle: it packages from a tree where nothing is
# built yet, it carries each listed tool and only what that tool needs, every
# tool runs from a copy of the package somewhere else, a tool whose interpreter
# or declared command the target lacks says so before it starts, and whatever
# cannot work on the target fails the configure or the package step.
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

# One run from an unpacked copy with a system PATH and nothing else in the
# environment. The exit status and the output are judged separately, so the
# expected text cannot stand in for a process that failed. The output has to
# equal the expected text, or to start with it when that text ends in "...".
expect_run() {
  local what=$1 want_rc=$2 want_out=$3 out rc
  shift 3
  out=$(cd / && env -i PATH=/usr/bin:/bin "$@" 2>&1)
  rc=$?
  [ "$rc" -eq "$want_rc" ] || note "$what exits $rc, expected $want_rc; it printed: $out"
  case "$want_out" in
  *...) [ "${out#"${want_out%...}"}" != "$out" ] || note "$what printed \"$out\", expected \"$want_out\"" ;;
  *) [ "$out" = "$want_out" ] || note "$what printed \"$out\", expected \"$want_out\"" ;;
  esac
}

# The Python tool of a packaged case, run from where it was staged.
py_tool() {
  printf '%s' "$scratch/$1/packages/FxBundle/tools/bin/fx-py"
}

# A case that packages; says so and shows the log when it does not.
packaged() {
  local name=$1
  shift
  if ! configure "$name" -DCASE=good "$@"; then
    note "$name: configure failed"
    show "$scratch/$name.cfg.log"
    return 1
  fi
  if ! package "$name"; then
    note "$name: the package step failed"
    show "$scratch/$name.pkg.log"
    return 1
  fi
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
    tools/bin/fx_cpp_tool tools/bin/fx-bin tools/libexec/fx-bin tools/bin/fx-script.sh \
    tools/bin/fx-py tools/lib/python/fxpkg/__init__.py tools/lib/python/fxdep/__init__.py \
    tools/lib/python/fxfast/__init__.py tools/REQUIREMENTS.txt; do
    [ -e "$pkg/$f" ] || note "good: the package has no $f"
  done
  ls "$pkg"/tools/libs/libfx_dep.so.1* >/dev/null 2>&1 ||
    note "good: the C++ tool's shared library is not in tools/libs"
  [ ! -e "$pkg/tools/lib/python/unrelated" ] ||
    note "good: the package carries a Python distribution only an unrequested extra requires"
  [ ! -e "$pkg/tools/lib/python/fxold" ] ||
    note "good: the package carries a Python distribution the tool does not require"
  [ ! -e "$pkg/tools/lib/python/bin" ] ||
    note "good: the package carries the build tree's console scripts"
  [ ! -e "$pkg/tools/libexec/fx-script.sh" ] ||
    note "good: a script with nothing to check is staged behind a launcher"
  rows=$(grep -c '^fx' "$pkg/tools/REQUIREMENTS.txt" 2>/dev/null)
  [ "$rows" = "7" ] || note "good: REQUIREMENTS.txt lists $rows tools, expected 7"
  # Each row states what its tool's owner declared and what its metadata asks.
  for row in \
    'fx-bin (bin): the system C library (.* Linux); on PATH: sh, fx-absent (the --absent operation)' \
    'fx-script.sh (script): a POSIX shell (/bin/sh)$' \
    'fx-needy.sh (script): a POSIX shell (/bin/sh); on PATH: fx-no-such-command' \
    'fx-interp.sh (script): /opt/fx-absent/interpreter$' \
    'fx-py (py): python3 matching >=3.8; carries fxdep 2.0, fxfast 1.0, fxpkg 1.0'; do
    grep -q -- "^$row" "$pkg/tools/REQUIREMENTS.txt" ||
      note "good: REQUIREMENTS.txt has no row \"$row\""
  done
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
  expect_run "good: the executive from the copy" 0 "fx_exec" "$moved/FxApp/bank_a/bin/fx_exec"
  expect_run "good: the C++ tool from the copy" 0 "fx_cpp_tool with fx_dep" "$moved/tools/bin/fx_cpp_tool"
  expect_run "good: the prebuilt tool from the copy" 0 "fx-bin" "$moved/tools/bin/fx-bin"
  expect_run "good: the shell tool from the copy" 0 "fx-script" "$moved/tools/bin/fx-script.sh"
  expect_run "good: the Python tool from the copy" 0 "fx-py with fxdep and fxfast as fx-py" \
    "$moved/tools/bin/fx-py"

  # A tool that prints what is expected of it and then fails: its status has to
  # reach the caller through the launcher, and this check has to see it.
  expect_run "good: the failing tool from the copy" 7 "fx-fails" "$moved/tools/bin/fx-fails.sh"

  # A command or an interpreter the target lacks stops the tool before it
  # starts, with one line naming what is missing.
  expect_run "good: the tool whose command is absent" 127 \
    "fx-needy.sh: no fx-no-such-command was found on this machine; it needs a POSIX shell (/bin/sh); on PATH: fx-no-such-command" \
    "$moved/tools/bin/fx-needy.sh"
  expect_run "good: the tool whose interpreter is absent" 127 \
    "fx-interp.sh: no /opt/fx-absent/interpreter was found on this machine; it needs /opt/fx-absent/interpreter" \
    "$moved/tools/bin/fx-interp.sh"

  # A target without python3 gets a plain message, not a traceback.
  mkdir "$scratch/empty-path"
  out=$(env -i PATH="$scratch/empty-path" "$moved/tools/bin/fx-py" 2>&1)
  rc=$?
  { [ "$rc" -eq 127 ] && printf '%s' "$out" | grep -q 'no python3 was found'; } ||
    note "good: without python3 the Python tool exits $rc and says: $out"
fi

# ------------------------------------------------------------------------------
# A Python tool runs only on an interpreter every staged distribution allows
# ------------------------------------------------------------------------------

# An upper bound on the tool's own distribution, and a floor on the one it
# requires: each refuses this machine's python3 before the tool is imported.
if packaged py-upper-bound -DFX_PKG_PYTHON='>=3.0,<3.1'; then
  expect_run "py-upper-bound: the Python tool" 1 \
    "fx-py: needs python3 matching >=3.6, <3.1; this machine has python ..." \
    "$(py_tool py-upper-bound)"
fi
if packaged py-dependency-floor -DFX_DEP_PYTHON='>=3.99'; then
  expect_run "py-dependency-floor: the Python tool" 1 \
    "fx-py: needs python3 matching >=3.99; this machine has python ..." \
    "$(py_tool py-dependency-floor)"
fi
# A requirement's marker is decided for the build's python3. One it does not
# select stays behind even though it is installed (fxold allows no python3, so
# carrying it would stop the tool), and the package says which answer its
# requirements were selected for. python_version is the minor version alone:
# no patch release of this python3 is above it.
this_python=$(PATH=/usr/bin:/bin python3 -c 'import sys; print("%d.%d.%d" % sys.version_info[:3])')
this_minor=${this_python%.*}
next_python=$this_minor.$((${this_python##*.} + 1))
if packaged py-version-marker -DFX_DEP_REQUIRES="fxold; python_version > \"$this_minor\""; then
  [ ! -e "$scratch/py-version-marker/packages/FxBundle/tools/lib/python/fxold" ] ||
    note "py-version-marker: the package carries a distribution whose marker does not select it"
  grep -q "^fx-py (py): python3 matching >=3.8 and python_version <= $this_minor (its requirements were selected for that); carries fxdep 2.0, fxfast 1.0, fxpkg 1.0\$" \
    "$scratch/py-version-marker/packages/FxBundle/tools/REQUIREMENTS.txt" ||
    note "py-version-marker: REQUIREMENTS.txt does not state the answer the requirements were selected for"
  expect_run "py-version-marker: the Python tool" 0 "fx-py with fxdep and fxfast as fx-py" \
    "$(py_tool py-version-marker)"
fi

# The launcher holds the target to that answer down to the patch release. The
# requirement here is selected from the next patch release on, so it is left
# out for this python3; a python3 that reports itself as that next release is
# refused before the tool is imported.
if packaged py-patch-marker -DFX_DEP_REQUIRES="fxnext; python_full_version > \"$this_python\""; then
  expect_run "py-patch-marker: the Python tool on the build's python3" 0 \
    "fx-py with fxdep and fxfast as fx-py" "$(py_tool py-patch-marker)"
  # Called as python3 -c <program> <arguments>, like the launcher calls it.
  mkdir "$scratch/next-patch"
  cat >"$scratch/next-patch/python3" <<'SHIM'
#!/bin/sh
program=$2
shift 2
PATH=/usr/bin:/bin
exec python3 -c '
import sys
info = sys.version_info
sys.version_info = info[:2] + (info[2] + 1,) + tuple(info[3:])
sys.version = "%d.%d.%d (fixture)" % sys.version_info[:3]
exec(sys.argv.pop(1))
' "$program" "$@"
SHIM
  chmod +x "$scratch/next-patch/python3"
  expect_run "py-patch-marker: the Python tool on the next patch release" 1 \
    "fx-py: needs python3 matching >=3.8 and python_full_version <= $this_python (its requirements were selected for that); this machine has python $next_python" \
    env PATH="$scratch/next-patch:/usr/bin:/bin" "$(py_tool py-patch-marker)"
fi

# What the launcher could not check, or the build cannot know, fails the
# package step.
if configure py-unchecked-clause -DCASE=good -DFX_DEP_PYTHON='>=3.8rc1'; then
  expect_package_error py-unchecked-clause 'tool fx-py cannot be staged' 'fxdep declares Requires-Python' \
    'the launcher cannot check'
else
  note "py-unchecked-clause: configure failed"
  show "$scratch/py-unchecked-clause.cfg.log"
fi
if configure py-kernel-marker -DCASE=good -DFX_DEP_REQUIRES='fxk; platform_release >= "5"'; then
  expect_package_error py-kernel-marker 'tool fx-py cannot be staged' 'depends on the kernel version'
else
  note "py-kernel-marker: configure failed"
  show "$scratch/py-kernel-marker.cfg.log"
fi

# A comparison installers do not all answer alike fails the package step too,
# naming the requirement. Each marker here would select fxold under one of the
# readings, or leave it out under another:
#   - a pre-release on the left ("3.x.yrc1" < the version is false for release
#     3.x.y, where the version > "3.x.yrc1" is true);
#   - text, or an extra's name, against something an installer may take for a
#     version: digits, a v or V before them, or the rest of a === comparison;
#   - an extra on the right of the comparison, where its own name is what the
#     installer may take for a version.
while IFS='|' read -r name marker text; do
  if configure "$name" -DCASE=good -DFX_DEP_REQUIRES="fxold; $marker"; then
    expect_package_error "$name" 'tool fx-py cannot be staged' \
      "cannot read fxdep's requirement" "$marker" "$text"
  else
    note "$name: configure failed"
    show "$scratch/$name.cfg.log"
  fi
done <<CASES
py-prerelease-on-the-left|"${this_python}rc1" < python_full_version|ordered against a pre-release
py-text-as-version|os_name != "3"|os_name is text, and '3' reads as a version
py-text-as-prefixed-version|os_name != " V3"|os_name is text, and ' V3' reads as a version
py-text-as-specifier|os_name == "=posix"|os_name is text, and '=posix' reads as a version
py-extra-as-version|extra != "v1"|the extra 'v1' reads as a version
py-extra-on-the-right|"fast" != extra|an extra is read on the left
CASES

# A requirement an active extra selects has to be installed, and so has one
# selected by an active extra together with the build's python3.
if configure py-missing-extra -DCASE=good; then
  rm -r "$scratch/py-missing-extra/pylib/fxfast-1.0.dist-info"
  expect_package_error py-missing-extra 'tool fx-py cannot be staged' 'fxdep requires fxfast'
else
  note "py-missing-extra: configure failed"
  show "$scratch/py-missing-extra.cfg.log"
fi
if configure py-missing-selected -DCASE=good \
  -DFX_DEP_REQUIRES='fxgone; extra == "fast" and python_version >= "3"'; then
  expect_package_error py-missing-selected 'tool fx-py cannot be staged' 'fxdep requires fxgone'
else
  note "py-missing-selected: configure failed"
  show "$scratch/py-missing-selected.cfg.log"
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
printf '[check-bundle-packaging] OK -- packages from an unbuilt tree; one tool of each kind runs from a copy elsewhere and a failing tool fails; a missing command, interpreter or python3 version stops a tool with one line; extras and markers select what a Python tool carries; foreign-architecture tools and unreadable metadata fail the package step; unknown, library and bare-metal tools fail the configure\n'
