# pkg_resolve.sh

ELF dependency resolver and application packager for the ApexFileSystem layout.

---

## Overview

`pkg_resolve.sh` resolves shared library dependencies via BFS on ELF `DT_NEEDED`
entries, stages the application binary and its project libraries into the
ApexFileSystem layout, and creates a deployable tarball. Works with
cross-compiled binaries; pass `--readelf` explicitly if auto-detection selects
the wrong one.

---

## Options

```
pkg_resolve.sh --app <name> --build-dir <path> [options]
```

| Option               | Description                                              |
| -------------------- | -------------------------------------------------------- |
| `--app <name>`       | Application binary name (required).                      |
| `--build-dir <path>` | Build directory containing `bin/` and `lib/` (required). |
| `--output <path>`    | Output directory (default: `<build-dir>/packages`).      |
| `--tprm <path>`      | TPRM master archive to include in the package.           |
| `--extra-bin <name>` | Additional binary to stage (e.g., watchdog). Repeatable. |
| `--readelf <path>`   | `readelf` binary to use (default: auto-detect).          |
| `--dry-run`          | Resolve dependencies only, do not stage files.           |
| `--verbose`          | Print dependency resolution details.                     |
| `--help`             | Show help.                                               |

---

## Output Structure

```
packages/<app>/
  bank_a/bin/<app>          Application binary
  bank_a/libs/*.so*         Shared library dependencies (with symlink chains)
  bank_a/tprm/master.tprm  TPRM config (if --tprm provided)
  run.sh                    Launch script
packages/<app>.tar.gz
```

The `run.sh` launch script reads the active bank marker, sets
`LD_LIBRARY_PATH`, and passes `--fs-root` and `--config` to the binary.

---

## Examples

```bash
# Package an application
pkg_resolve.sh --app ApexHilDemo --build-dir build/rpi-aarch64-release

# Include TPRM configuration
pkg_resolve.sh --app ApexHilDemo \
  --build-dir build/rpi-aarch64-release \
  --tprm apps/apex_hil_demo/tprm/master.tprm

# Package with watchdog
pkg_resolve.sh --app ApexHilDemo \
  --build-dir build/rpi-aarch64-release \
  --extra-bin ApexWatchdog \
  --tprm apps/apex_hil_demo/tprm/master.tprm

# Dry run (resolve only, print dependencies)
pkg_resolve.sh --app ApexHilDemo \
  --build-dir build/rpi-aarch64-release \
  --dry-run --verbose

# Cross-compiled binary with explicit readelf
pkg_resolve.sh --app ApexHilDemo \
  --build-dir build/rpi-aarch64-release \
  --readelf aarch64-linux-gnu-readelf
```
