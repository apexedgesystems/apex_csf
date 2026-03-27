# Rust Tools Build System

Technical documentation for the Rust tools build infrastructure.

---

## Overview

Rust tools are built using Cargo and deployed as native binaries via a CMake
custom target that produces a self-contained, relocatable build directory.

**Design Goals:**

- Build directory is self-contained (copy anywhere and run)
- No hardcoded tool names in build infrastructure
- Native binaries with no runtime dependencies
- Clean separation: binaries in `bin/tools/rust/`
- Incremental builds via stamp file pattern

---

## Architecture

### Directory Layout (Build Output)

```
build/native-linux-debug/
+-- .env                        # Environment setup (PATH includes rust tools)
+-- bin/
|   \-- tools/
|       +-- py/                 # Python tools
|       \-- rust/               # Rust tools
|           +-- serial_dev_checker
|           +-- serial_dev_tester
|           +-- tprm_template
|           +-- tprm_pack
|           +-- apex_data_gen
|           +-- rdat_tool
|           +-- sdat_tool
|           +-- upx_tool
|           \-- cfg2bin
\-- rust-target/                # Cargo build directory (intermediate)
    \-- release/
        \-- ...
```

### Source Layout

```
tools/rust/
+-- Cargo.toml              # Package definition
+-- Cargo.lock              # Locked dependencies
+-- build.rs                # Build script (CUDA detection)
+-- README.md               # User documentation
+-- CMakeLists.txt          # CMake build integration
+-- docs/
|   +-- RUST_TOOLS_BUILD.md     # This file
|   +-- serial_dev_checker.md   # Serial checker usage
|   +-- serial_dev_tester.md    # Serial tester usage
|   +-- tprm_template.md        # Template generator usage
|   +-- rdat_tool.md            # Registry RDAT analysis
|   +-- upx_tool.md             # UPX tool usage
|   \-- cfg2bin.md              # Config to binary usage
+-- src/
|   +-- lib.rs              # Library root
|   +-- bin/
|   |   +-- serial_dev_checker.rs
|   |   +-- serial_dev_tester.rs
|   |   +-- tprm_template.rs
|   |   +-- tprm_pack.rs
|   |   +-- apex_data_gen.rs
|   |   +-- rdat_tool.rs
|   |   +-- sdat_tool.rs
|   |   +-- upx_tool.rs
|   |   \-- cfg2bin.rs
|   +-- serial/             # Serial device module
|   +-- tunable_params/     # C++ parsing module
|   \-- upx/                # UPX operations module
\-- tests/
    \-- ...                 # Integration tests
```

---

## Build Process

### Cargo Release Build

Unlike Python (which requires pip install), Rust produces standalone binaries:

```
cargo build --release --target-dir ${BUILD_DIR}/rust-target
    \-- rust-target/release/
        +-- serial_dev_checker
        +-- serial_dev_tester
        +-- tprm_template
        +-- upx_tool
        \-- cfg2bin
              v
    Copy to bin/tools/rust/
```

### CMakeLists.txt

The CMake file knows NOTHING about individual tools. All tool definitions live
in Cargo.toml. Adding a new `[[bin]]` target automatically includes it.

```cmake
# tools/rust/CMakeLists.txt

# Prerequisites: cargo
find_program(CARGO_EXECUTABLE cargo)
if (NOT CARGO_EXECUTABLE)
  message(WARNING "Cargo not found; skipping Rust tools")
  return()
endif ()

# Paths
set(_rust_src "${CMAKE_CURRENT_SOURCE_DIR}")
set(_rust_bin_dir "${CMAKE_BINARY_DIR}/bin/tools/rust")
set(_rust_target_dir "${CMAKE_BINARY_DIR}/rust-target")
set(_stamp_file "${CMAKE_BINARY_DIR}/.rust_tools_installed")

# Source dependency tracking
file(GLOB_RECURSE _rust_sources
  "${_rust_src}/src/*.rs"
  "${_rust_src}/Cargo.toml"
  "${_rust_src}/Cargo.lock"
  "${_rust_src}/build.rs"
)

add_custom_command(
  OUTPUT "${_stamp_file}"
  COMMAND ${CARGO_EXECUTABLE} build --release --target-dir "${_rust_target_dir}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${_rust_bin_dir}"
  # Copy all executables dynamically - no hardcoded tool names
  COMMAND sh -c "find ... -type f -executable -exec cp {} '${_rust_bin_dir}/' \\;"
  COMMAND ${CMAKE_COMMAND} -E touch "${_stamp_file}"
  DEPENDS ${_rust_sources}
  WORKING_DIRECTORY "${_rust_src}"
  COMMENT "Building Rust tools"
)

add_custom_target(rust_tools DEPENDS "${_stamp_file}")
```

### Makefile Integration

```makefile
# mk/tools.mk

tools-rust: prep
    $(call log,tools,Building Rust tools)
    @test -f "$(BUILD_DIR)/CMakeCache.txt" || cmake --preset $(HOST_DEBUG_PRESET)
    @cd "$(BUILD_DIR)" && ninja rust_tools
    @grep -q 'bin/tools/rust' "$(BUILD_DIR)/.env" 2>/dev/null || \
      printf 'export PATH="$$PWD/bin/tools/rust:$$PATH"\n' >> "$(BUILD_DIR)/.env"
    $(call log,tools,Rust tools ready - source .env from build directory to use)
```

Key points:

- `prep` target creates empty `.env` file
- `tools-rust` appends Rust PATH only if not already present (idempotent)
- Uses `$PWD` for relative paths (relocatable)

---

## The .env File

### Purpose

Unlike Python, Rust binaries do not require PYTHONPATH or library paths.
Only PATH is needed to find the binaries.

### Content

```bash
# build/native-linux-debug/.env
export PATH="$PWD/bin/tools/py:$PATH"
export PYTHONPATH="$PWD/lib/python:$PYTHONPATH"
export PATH="$PWD/bin/tools/rust:$PATH"
```

- Uses `$PWD` so paths are relative to where you source from
- Appends to existing PATH (doesn't clobber)
- Must be sourced from the build directory

### Usage Pattern

```bash
# From project root
make tools-rust

# Enter build directory and source .env
cd build/native-linux-debug
source .env

# Now tools work
serial_dev_checker --help
upx_tool --help
```

### Relocatable Builds

Because `.env` uses `$PWD`, the build directory is relocatable:

```bash
# Copy build directory to another machine
scp -r build/native-linux-debug user@remote:/opt/apex/

# On remote machine
cd /opt/apex/native-linux-debug
source .env
serial_dev_checker --help  # Works!
```

---

## CUDA Feature

The Rust tools support optional CUDA integration via a cargo feature:

```toml
# Cargo.toml
[features]
default = []
cuda = ["dep:cuda-sys"]
```

The CMake build detects CUDA presence and enables the feature automatically:

```cmake
if (DEFINED ENV{CUDA_HOME} OR EXISTS "/usr/local/cuda")
  set(_cargo_features "--features" "cuda")
endif ()
```

The `build.rs` script performs additional CUDA toolkit detection at compile time.

---

## Adding New Tools

### 1. Create the Binary Source

```rust
// tools/rust/src/bin/mytool.rs

use clap::Parser;

#[derive(Parser)]
#[command(name = "mytool", about = "My tool description")]
struct Args {
    #[arg(short, long)]
    config: String,
}

fn main() {
    let args = Args::parse();
    // ... implementation
}
```

### 2. Add Binary to Cargo.toml

```toml
# tools/rust/Cargo.toml

[[bin]]
name = "mytool"
path = "src/bin/mytool.rs"
```

### 3. Rebuild

```bash
make tools-rust
```

The new tool automatically appears in `bin/tools/rust/mytool`.

---

## Comparison with Python Tools

| Aspect          | Python                     | Rust                     |
| --------------- | -------------------------- | ------------------------ |
| Package manager | Poetry                     | Cargo                    |
| Build artifact  | Wheel + pip install        | Native binaries          |
| Dependencies    | Installed to lib/python/   | Statically linked        |
| Scripts         | pip-generated entry points | Direct executables       |
| .env needs      | PYTHONPATH + PATH          | PATH only                |
| Build speed     | Slow (pip installs)        | Fast (incremental cargo) |
| Runtime deps    | Python interpreter         | None                     |

---

## Testing

### Unit Tests

```bash
# Run via make (future target)
make test-rust

# Or directly with cargo
cd tools/rust && cargo test
```

### Integration Test

```bash
# Clean build
make distclean
make tools-rust

# Verify tools work
cd build/native-linux-debug
source .env
serial_dev_checker --help
```

---

## Troubleshooting

### "Command not found: serial_dev_checker"

**Cause:** PATH not set or wrong directory.

**Fix:** Source `.env` from the build directory (not project root).

### "Cargo not found"

**Cause:** Rust toolchain not installed.

**Fix:** Install Rust via rustup:

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

### Build Fails with CUDA Errors

**Cause:** CUDA feature enabled but toolkit not properly installed.

**Fix:** Either install CUDA toolkit or disable the feature:

```bash
# In tools/rust/
cargo build --release --no-default-features
```

### Incremental Build Not Working

**Cause:** Stamp file exists but sources changed.

**Fix:** Clean and rebuild:

```bash
make clean-rust
make tools-rust
```

---

## See Also

- [tools/rust/README.md](../README.md) - User documentation
- [serial_dev_checker.md](serial_dev_checker.md) - Serial checker usage
- [upx_tool.md](upx_tool.md) - UPX tool usage
- [mk/tools.mk](../../../mk/tools.mk) - Makefile integration
