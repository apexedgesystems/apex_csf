# Apex Make Build System

Unified Make interface for building, testing, and packaging the Apex CSF C++
project. Wraps CMake presets, CTest, Docker, and development tools behind
consistent targets.

## Quick Start

```bash
# Build and test (native debug)
make
make testp

# Build native release
make release

# Release an app for all its platforms
make release APP=ApexDemo

# See all available targets
make help
```

## Targets

### Native Builds

| Target                   | Description                  |
| ------------------------ | ---------------------------- |
| `make` / `make debug`    | Build native debug (default) |
| `make release`           | Build native release         |
| `make docs`              | Build Doxygen documentation  |
| `make configure`         | Configure only (no build)    |
| `make configure-release` | Configure release only       |

### Cross-Compilation

| Target                | Platform      | Architecture   |
| --------------------- | ------------- | -------------- |
| `make jetson-debug`   | NVIDIA Jetson | aarch64 + CUDA |
| `make jetson-release` | NVIDIA Jetson | aarch64 + CUDA |
| `make rpi-debug`      | Raspberry Pi  | aarch64        |
| `make rpi-release`    | Raspberry Pi  | aarch64        |
| `make riscv-debug`    | RISC-V Linux  | riscv64        |
| `make riscv-release`  | RISC-V Linux  | riscv64        |

Configure-only variants available: `configure-jetson`, `configure-rpi`, etc.

### Firmware

| Target               | Platform | Description               |
| -------------------- | -------- | ------------------------- |
| `make stm32`         | STM32    | Build bare-metal firmware |
| `make arduino`       | Arduino  | Build AVR firmware        |
| `make pico`          | Pico     | Build RP2040 firmware     |
| `make esp32`         | ESP32    | Build ESP-IDF firmware    |
| `make stm32-flash`   | STM32    | Flash via ST-Link         |
| `make arduino-flash` | Arduino  | Flash via avrdude         |
| `make pico-flash`    | Pico     | Flash via picotool        |
| `make esp32-flash`   | ESP32    | Flash via esptool         |

### Testing

| Target           | Description                                |
| ---------------- | ------------------------------------------ |
| `make test`      | Run all C++ tests serially                 |
| `make testp`     | Run tests in parallel, timing tests serial |
| `make test-py`   | Run Python tools tests                     |
| `make test-rust` | Run Rust tools tests                       |

### Code Quality

| Target              | Description                                            |
| ------------------- | ------------------------------------------------------ |
| `make format`       | Auto-fix formatting (clang-format, cmake-format, etc.) |
| `make format-check` | Check formatting without fixing                        |
| `make coverage`     | Generate code coverage report                          |
| `make static`       | Run static analysis (scan-build)                       |
| `make asan`         | Build and test with AddressSanitizer                   |
| `make tsan`         | Build and test with ThreadSanitizer                    |
| `make ubsan`        | Build and test with UndefinedBehaviorSanitizer         |

### Tools

| Target              | Description                         |
| ------------------- | ----------------------------------- |
| `make tools`        | Build all tools (C++, Rust, Python) |
| `make tools-cpp`    | Build C++ tools only                |
| `make tools-rust`   | Build Rust tools only               |
| `make tools-py`     | Build Python tools only             |
| `make apex-data-db` | Generate struct dictionaries        |

### Release

| Target                    | Description                              |
| ------------------------- | ---------------------------------------- |
| `make release APP=<name>` | Build + package all platforms for an app |
| `make release-all`        | Release all registered apps              |
| `make release-clean`      | Remove `release/` directory              |

Apps declare their platforms in `apps/<app>/release.mk`. The release target
builds each platform via Docker Compose, packages POSIX binaries with shared
library dependencies (via `pkg_resolve.sh`), copies firmware artifacts for
bare-metal targets, and creates a combined tarball. POSIX packages include
TPRM configuration and a launch script.

Output structure:

```
release/
  <APP>/
    <platform>/
      bank_a/bin/<binary>       # POSIX executable
      bank_a/libs/*.so          # Shared library dependencies
      bank_a/tprm/master.tprm  # TPRM configuration
      run.sh                    # Launch script (auto-configures paths)
      firmware/*.elf            # Bare-metal firmware (firmware platforms)
  <APP>.tar.gz                  # Combined tarball
```

### Docker

| Target                  | Description                      |
| ----------------------- | -------------------------------- |
| `make shell-dev`        | Enter CPU development shell      |
| `make shell-dev-cuda`   | Enter CUDA development shell     |
| `make shell-dev-jetson` | Enter Jetson cross-compile shell |
| `make shell-dev-stm32`  | Enter STM32 embedded shell       |
| `make docker-all`       | Build all Docker images          |
| `make artifacts`        | Extract release tarballs         |

See `make help` for full list of Docker, shell, and compose targets.

### Compose (build via Docker Compose)

| Target                  | Description                      |
| ----------------------- | -------------------------------- |
| `make compose-debug`    | Native debug via dev-cuda        |
| `make compose-release`  | Native release via dev-cuda      |
| `make compose-testp`    | Parallel tests via dev-cuda      |
| `make compose-coverage` | Coverage report via dev-cuda     |
| `make compose-format`   | Format code via dev-cuda         |
| `make compose-stm32`    | STM32 firmware via dev-stm32     |
| `make compose-arduino`  | Arduino firmware via dev-arduino |
| `make compose-pico`     | Pico firmware via dev-pico       |
| `make compose-esp32`    | ESP32 firmware via dev-esp32     |

All `compose-*` targets run `make <target>` inside the correct Docker Compose
service, passing through `VERBOSE` and `CMAKE_EXTRA_ARGS`.

### Cleanup

| Target              | Description                                  |
| ------------------- | -------------------------------------------- |
| `make clean`        | Clean build artifacts (ninja clean + extras) |
| `make distclean`    | Remove entire `build/` directory             |
| `make docker-clean` | Remove dangling Docker images                |
| `make docker-prune` | Remove all `apex.*` images                   |

## Configuration

### Variables

Override at invocation: `make debug NUM_JOBS=8 VERBOSE=1`

| Variable           | Default                    | Description                                  |
| ------------------ | -------------------------- | -------------------------------------------- |
| `NUM_JOBS`         | Auto (nproc)               | Parallel job count                           |
| `VERBOSE`          | 0                          | Set to 1 for CMake per-target output         |
| `BUILD_DIR`        | `build/native-linux-debug` | Build directory for test/coverage/sanitizers |
| `LLVM_VER`         | 21                         | LLVM/Clang version for coverage              |
| `CMAKE_EXTRA_ARGS` | (empty)                    | Extra CMake arguments passed through         |
| `APP`              | (empty)                    | App name for release/package targets         |

### CMake Presets

Targets use presets from `CMakePresets.json`:

| Make Target      | CMake Preset             |
| ---------------- | ------------------------ |
| `debug`          | `native-linux-debug`     |
| `release`        | `native-linux-release`   |
| `jetson-debug`   | `jetson-aarch64-debug`   |
| `jetson-release` | `jetson-aarch64-release` |
| `rpi-debug`      | `rpi-aarch64-debug`      |
| `rpi-release`    | `rpi-aarch64-release`    |
| `riscv-debug`    | `riscv64-linux-debug`    |
| `riscv-release`  | `riscv64-linux-release`  |
| `stm32`          | `stm32-baremetal`        |
| `arduino`        | `arduino-baremetal`      |
| `pico`           | `pico-baremetal`         |
| `esp32`          | `esp32-baremetal`        |

## Usage Examples

### Development Workflow

```bash
# Initial build
make debug

# Edit code, rebuild (incremental)
make debug

# Run tests
make testp

# Check formatting before commit
make format-check

# Fix formatting
make format
```

### Release Workflow

```bash
# Release a single app (builds all its platforms, packages, creates tarball)
make release APP=ApexDemo

# Release all registered apps
make release-all

# Check output
ls release/ApexDemo/native/bin/
tar tzf release/ApexDemo.tar.gz
```

### CI Artifact Build

```bash
# Build release artifacts in Docker (legacy workflow)
make docker-builders
make docker-final
make artifacts
```

### Debugging Memory Issues

```bash
# AddressSanitizer (buffer overflows, use-after-free)
make asan

# ThreadSanitizer (data races)
make tsan

# UndefinedBehaviorSanitizer (UB, integer overflow)
make ubsan
```

### Code Coverage

```bash
# Generate coverage report
make coverage

# View report
open build/native-linux-debug/coverage/*/html/index.html
```

### Cross-Compilation

```bash
# Option 1: Use Docker Compose (recommended)
make compose-jetson-release

# Option 2: Use Docker shell
make shell-dev-jetson
# Inside container:
make jetson-release

# Option 3: Direct (requires toolchain installed)
make jetson-release
```

## Output Structure

```
build/
+-- native-linux-debug/       # make debug
|   +-- bin/                  # Executables
|   +-- lib/                  # Libraries
|   +-- compile_commands.json # For IDE/clangd
|   \-- coverage/             # Coverage reports
+-- native-linux-release/     # make release
+-- jetson-aarch64-release/   # make jetson-release
+-- rpi-aarch64-release/      # make rpi-release
+-- stm32/                    # make stm32
|   \-- firmware/             # .elf, .bin, .hex
\-- ...

release/                      # make release APP=<name>
+-- <APP>/
|   \-- <platform>/
\-- <APP>.tar.gz

compile_commands.json         # Symlink to active build
```

## File Organization

```
Makefile                 # Entry point, native build targets
mk/
+-- README.md            # This file
+-- common.mk            # Shared config (NUM_JOBS, logging, BUILD_DIR)
+-- test.mk              # CTest wrappers (test, testp, test-py, test-rust)
+-- coverage.mk          # LLVM coverage instrumentation and reporting
+-- sanitizers.mk        # ASan/TSan/UBSan build and test
+-- format.mk            # Pre-commit formatting (format, format-check)
+-- tools.mk             # Tool builds, static analysis, data generation
+-- firmware.mk          # Firmware flashing and reset (STM32, Arduino, Pico, ESP32)
+-- docker.mk            # Docker image builds and management
+-- compose.mk           # Docker Compose wrappers for all targets
+-- release.mk           # Release packaging (per-app, multi-platform)
\-- clean.mk             # Artifact cleanup (clean, distclean)
```

### Module Overview

| Module          | Purpose                                        | Platform    |
| --------------- | ---------------------------------------------- | ----------- |
| `common.mk`     | Shared variables, logging helpers, `BUILD_DIR` | All         |
| `test.mk`       | CTest wrappers (serial, parallel, py, rust)    | Native only |
| `coverage.mk`   | LLVM source-based code coverage                | Native only |
| `sanitizers.mk` | ASan/TSan/UBSan builds                         | Native only |
| `format.mk`     | Pre-commit formatting and lint                 | All         |
| `tools.mk`      | Tool builds, static analysis, data generation  | Native only |
| `firmware.mk`   | Flash and reset for embedded targets           | Embedded    |
| `docker.mk`     | Container image builds and shells              | All         |
| `compose.mk`    | Docker Compose wrappers for build targets      | All         |
| `release.mk`    | Multi-platform release packaging               | All         |
| `clean.mk`      | Artifact cleanup                               | All         |

## Adding New Modules

1. Create `mk/newmodule.mk`:

```makefile
# ==============================================================================
# mk/newmodule.mk - Brief description
#
# Longer description of what this module provides.
# ==============================================================================

ifndef NEWMODULE_MK_GUARD
NEWMODULE_MK_GUARD := 1

# ------------------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------------------

# Uses BUILD_DIR and NUM_JOBS from common.mk

# ------------------------------------------------------------------------------
# Targets
# ------------------------------------------------------------------------------

my-target:
    $(call log,mytag,Doing something)
    @some-command

# ------------------------------------------------------------------------------
# Phony Declarations
# ------------------------------------------------------------------------------

.PHONY: my-target

endif  # NEWMODULE_MK_GUARD
```

2. Add `include mk/newmodule.mk` to root Makefile (after `common.mk`)
3. Add targets to `make help` output

## Troubleshooting

### "No rule to make target"

Ensure you're in the project root (where `Makefile` is located).

### Tests Fail with Library Not Found

The test targets set `LD_LIBRARY_PATH` automatically. If running ctest manually:

```bash
cd build/native-linux-debug
LD_LIBRARY_PATH=$PWD/lib:$LD_LIBRARY_PATH ctest
```

### Coverage Shows 0%

- Coverage requires Clang (not GCC)
- Must be Debug build
- Tests must link project libraries (auto-detected from `LINK`)

### Sanitizer Builds Slow

Sanitizers add overhead. For quick iteration:

```bash
cd build/native-linux-debug
LD_LIBRARY_PATH=$PWD/lib ctest -R MySpecificTest
```

### ccache Not Working

```bash
# Check stats
make ccache-stats

# Verify it's configured
grep CMAKE_CXX_COMPILER_LAUNCHER build/native-linux-debug/CMakeCache.txt
```

### Pre-commit Hooks

Install hooks for automatic formatting on commit:

```bash
make PRE_COMMIT_INSTALL=yes prep
```
