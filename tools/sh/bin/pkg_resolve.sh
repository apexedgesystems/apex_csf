#!/bin/bash
# ==============================================================================
# pkg_resolve.sh - ELF dependency resolver and application packager
#
# Resolves shared library dependencies for a POSIX application binary using
# readelf, stages the binary and its project libraries into a deployable
# directory structure, and optionally includes TPRM configuration and a
# launch script.
#
# Usage:
#   pkg_resolve.sh --app <name> --build-dir <path> [options]
#
# Options:
#   --app <name>          Application binary name (required)
#   --build-dir <path>    Build directory containing bin/ and lib/ (required)
#   --output <path>       Output directory (default: <build-dir>/packages)
#   --tprm <path>         TPRM master archive to include in package
#   --readelf <path>      readelf binary to use (default: auto-detect)
#   --dry-run             Resolve only, do not stage files
#   --verbose             Print dependency resolution details
#   --help                Show this help
#
# Output (matches ApexFileSystem layout):
#   <output>/<app>/bank_a/bin/<app>          Application binary
#   <output>/<app>/bank_a/libs/*.so*         Shared library dependencies
#   <output>/<app>/bank_a/tprm/master.tprm  TPRM config (if --tprm provided)
#   <output>/<app>/run.sh                   Launch script
#   <output>/<app>.tar.gz                   Deployable tarball
#
# Algorithm:
#   BFS on ELF DT_NEEDED entries. For each NEEDED library, checks if it
#   exists in the build lib/ directory (project library) or is a system
#   library (skipped). Handles versioned symlink chains (e.g.,
#   libfmt.so.11 -> libfmt.so.11.1.4). Cross-architecture safe: works
#   with any readelf that supports the target architecture.
#
# ==============================================================================

set -euo pipefail

# ==============================================================================
# Constants
# ==============================================================================

readonly MAX_SYMLINK_DEPTH=10
readonly SCRIPT_NAME="pkg_resolve"

# ==============================================================================
# Logging
# ==============================================================================

log() { printf '\033[36m[%s]\033[0m %s\n' "$SCRIPT_NAME" "$1" >&2; }
log_ok() { printf '\033[32m[%s]\033[0m %s\n' "$SCRIPT_NAME" "$1" >&2; }
err() { printf '\033[31m[%s]\033[0m %s\n' "$SCRIPT_NAME" "$1" >&2; }
verbose() { if [[ "$VERBOSE" == "1" ]]; then log "$1"; fi; }

# ==============================================================================
# CLI Parsing
# ==============================================================================

APP=""
BUILD_DIR=""
OUTPUT_DIR=""
TPRM_PATH=""
READELF_BIN=""
DRY_RUN=0
VERBOSE=0
EXTRA_BINS=()

usage() {
  sed -n '2,/^# ==/{ /^#/s/^# \{0,1\}//p }' "$0"
  exit 0
}

while [[ $# -gt 0 ]]; do
  case "$1" in
  --app)
    APP="$2"
    shift 2
    ;;
  --build-dir)
    BUILD_DIR="$2"
    shift 2
    ;;
  --output)
    OUTPUT_DIR="$2"
    shift 2
    ;;
  --tprm)
    TPRM_PATH="$2"
    shift 2
    ;;
  --extra-bin)
    EXTRA_BINS+=("$2")
    shift 2
    ;;
  --readelf)
    READELF_BIN="$2"
    shift 2
    ;;
  --dry-run)
    DRY_RUN=1
    shift
    ;;
  --verbose)
    VERBOSE=1
    shift
    ;;
  --help | -h) usage ;;
  *)
    err "Unknown option: $1"
    exit 1
    ;;
  esac
done

# Validate required args
[[ -z "$APP" ]] && {
  err "--app is required"
  exit 1
}
[[ -z "$BUILD_DIR" ]] && {
  err "--build-dir is required"
  exit 1
}
[[ -d "$BUILD_DIR" ]] || {
  err "Build directory not found: $BUILD_DIR"
  exit 1
}

# Defaults
OUTPUT_DIR="${OUTPUT_DIR:-$BUILD_DIR/packages}"

# ==============================================================================
# Find readelf
# ==============================================================================

find_readelf() {
  if [[ -n "$READELF_BIN" ]]; then
    command -v "$READELF_BIN" >/dev/null 2>&1 || {
      err "readelf not found: $READELF_BIN"
      exit 1
    }
    return
  fi

  # Try cross-compile readelf first (handles aarch64 binaries on x86_64 host)
  for candidate in readelf aarch64-linux-gnu-readelf arm-linux-gnueabihf-readelf; do
    if command -v "$candidate" >/dev/null 2>&1; then
      READELF_BIN="$candidate"
      return
    fi
  done

  err "readelf not found on PATH"
  exit 1
}

find_readelf

# ==============================================================================
# Find application binary
# ==============================================================================

find_binary() {
  local bin_path="$BUILD_DIR/bin/$APP"
  if [[ -x "$bin_path" ]]; then
    echo "$bin_path"
    return
  fi

  # Search subdirectories of bin/
  local found
  found=$(find "$BUILD_DIR/bin" -name "$APP" -type f -executable 2>/dev/null | head -1)
  if [[ -n "$found" ]]; then
    echo "$found"
    return
  fi

  err "Binary not found: $APP (searched $BUILD_DIR/bin/)"
  exit 1
}

APP_BINARY=$(find_binary)
LIB_DIR="$BUILD_DIR/lib"
verbose "Binary: $APP_BINARY"
verbose "Lib dir: $LIB_DIR"

# ==============================================================================
# ELF Dependency Resolution (BFS)
# ==============================================================================

# Extract DT_NEEDED entries from an ELF binary/library
get_needed() {
  local elf_path="$1"
  "$READELF_BIN" -d "$elf_path" 2>/dev/null |
    grep '(NEEDED)' |
    sed -n 's/.*\[\(.*\)\]/\1/p'
}

# Resolve all project library dependencies via BFS
resolve_deps() {
  local -a queue=("$APP_BINARY")

  # Seed queue with extra binaries so their deps are also resolved
  for extra in "${EXTRA_BINS[@]}"; do
    local extra_path="$BUILD_DIR/bin/$extra"
    if [[ -x "$extra_path" ]]; then
      queue+=("$extra_path")
    fi
  done

  local -A visited=()
  local -a resolved=()

  while [[ ${#queue[@]} -gt 0 ]]; do
    local current="${queue[0]}"
    queue=("${queue[@]:1}")

    local basename
    basename=$(basename "$current")

    # Skip if already visited
    [[ -n "${visited[$basename]:-}" ]] && continue
    visited["$basename"]=1

    verbose "Scanning: $basename"

    local needed
    needed=$(get_needed "$current")

    local lib
    for lib in $needed; do
      [[ -n "${visited[$lib]:-}" ]] && continue

      local lib_path="$LIB_DIR/$lib"
      if [[ -e "$lib_path" ]]; then
        verbose "  Project lib: $lib"
        resolved+=("$lib")
        queue+=("$lib_path")
      else
        verbose "  System lib (skip): $lib"
      fi
    done
  done

  printf '%s\n' "${resolved[@]}"
}

log "Resolving dependencies for $APP"
RESOLVED_LIBS=$(resolve_deps)
LIB_COUNT=$(echo "$RESOLVED_LIBS" | grep -c . || true)
log "Found $LIB_COUNT project libraries"

if [[ "$DRY_RUN" == "1" ]]; then
  log "Dry run -- resolved libraries:"
  echo "$RESOLVED_LIBS" | sort
  exit 0
fi

# ==============================================================================
# Stage Files
# ==============================================================================

STAGE_DIR="$OUTPUT_DIR/$APP"
rm -rf "$STAGE_DIR"

# Stage into ApexFileSystem layout: bank_a/{bin,libs,tprm} is the active bank.
# The deployment directory IS the filesystem root (--fs-root passed by run.sh).
mkdir -p "$STAGE_DIR/bank_a/bin" "$STAGE_DIR/bank_a/libs"

# Copy binary into active bank
cp -f "$APP_BINARY" "$STAGE_DIR/bank_a/bin/$APP"
chmod 755 "$STAGE_DIR/bank_a/bin/$APP"
verbose "Staged binary: bank_a/bin/$APP"

# Copy extra binaries declared by release manifest
for extra in "${EXTRA_BINS[@]}"; do
  local_path="$BUILD_DIR/bin/$extra"
  if [[ -x "$local_path" ]]; then
    cp -f "$local_path" "$STAGE_DIR/bank_a/bin/$extra"
    chmod 755 "$STAGE_DIR/bank_a/bin/$extra"
    verbose "Staged binary: bank_a/bin/$extra"
  else
    err "Extra binary not found: $local_path"
    exit 1
  fi
done

# Copy libraries with symlink chain resolution
copy_lib_with_chain() {
  local lib_name="$1"
  local lib_path="$LIB_DIR/$lib_name"
  local depth=0

  while [[ $depth -lt $MAX_SYMLINK_DEPTH ]]; do
    if [[ -e "$STAGE_DIR/bank_a/libs/$lib_name" ]]; then
      return # Already copied
    fi

    if [[ -L "$lib_path" ]]; then
      # Symlink: copy the link itself, then follow the chain
      cp -P "$lib_path" "$STAGE_DIR/bank_a/libs/$lib_name"
      verbose "  Staged symlink: bank_a/libs/$lib_name"

      local target
      target=$(readlink "$lib_path")
      lib_name=$(basename "$target")
      lib_path="$LIB_DIR/$lib_name"
      depth=$((depth + 1))
    else
      # Regular file: copy it
      cp -f "$lib_path" "$STAGE_DIR/bank_a/libs/$lib_name"
      verbose "  Staged library: bank_a/libs/$lib_name"
      return
    fi
  done

  err "Symlink chain too deep for $1 (max $MAX_SYMLINK_DEPTH)"
}

while IFS= read -r lib; do
  [[ -z "$lib" ]] && continue
  copy_lib_with_chain "$lib"
done <<<"$RESOLVED_LIBS"

# ==============================================================================
# Include TPRM Configuration
# ==============================================================================

if [[ -n "$TPRM_PATH" && -f "$TPRM_PATH" ]]; then
  mkdir -p "$STAGE_DIR/bank_a/tprm"
  cp -f "$TPRM_PATH" "$STAGE_DIR/bank_a/tprm/master.tprm"
  verbose "Staged TPRM: bank_a/tprm/master.tprm"
fi

# ==============================================================================
# Generate Launch Script
# ==============================================================================

cat >"$STAGE_DIR/run.sh" <<'LAUNCH_EOF'
#!/bin/bash
# Auto-generated launch script
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

BIN_NAME="$1"; shift
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
LAUNCH_EOF

chmod 755 "$STAGE_DIR/run.sh"
verbose "Generated launch script: run.sh"

# ==============================================================================
# Create Tarball
# ==============================================================================

tar -czf "$OUTPUT_DIR/$APP.tar.gz" -C "$OUTPUT_DIR" "$APP"
TARBALL_SIZE=$(du -sh "$OUTPUT_DIR/$APP.tar.gz" | cut -f1)
log_ok "Package complete: $OUTPUT_DIR/$APP.tar.gz ($TARBALL_SIZE)"
