# Apex CMake Build System

Modular CMake infrastructure for the Apex CSF C++ project. Provides consistent
target creation, CUDA integration, testing, cross-compilation, bare-metal
firmware, and release packaging support.

## Quick Start

```cmake
# In your module's CMakeLists.txt
include(apex/All)  # or individual modules as needed

apex_add_library(
  NAME   mylib
  TYPE   SHARED
  INC    ${CMAKE_CURRENT_SOURCE_DIR}/inc
  SRC    src/foo.cpp src/bar.cpp
)
```

## Modules

All modules are included via `All.cmake` in strict dependency order:

| Module                    | Purpose                                               |
| ------------------------- | ----------------------------------------------------- |
| `Core.cmake`              | Guards, module helpers, standard opt-ins              |
| `BuildAcceleration.cmake` | ccache, fast linker (mold/lld), split DWARF           |
| `Cuda.cmake`              | CUDA sources, NVML, CUPTI integration                 |
| `Targets.cmake`           | `apex_add_library`, `apex_add_app`, `apex_add_tool`   |
| `Coverage.cmake`          | Code coverage instrumentation and reporting           |
| `Testing.cmake`           | GTest/GMock, performance tests, development tests     |
| `Tooling.cmake`           | Doxygen, clang-tidy, UPX compression                  |
| `DataDefinitions.cmake`   | Struct dictionary manifest registration               |
| `Packaging.cmake`         | Application deployment packaging (`package_<APP>`)    |
| `Firmware.cmake`          | Bare-metal firmware targets (STM32, AVR, Pico, ESP32) |
| `Summary.cmake`           | Configure-time build environment summary              |
| `All.cmake`               | Convenience include for all modules                   |

## Configuration Options

### Build Types

```bash
cmake -DCMAKE_BUILD_TYPE=Debug    # Debug symbols, no optimization
cmake -DCMAKE_BUILD_TYPE=Release  # Full optimization, NDEBUG defined
```

### Feature Toggles

| Variable               | Default | Description                           |
| ---------------------- | ------- | ------------------------------------- |
| `APEX_TARGETS_VERBOSE` | OFF     | Print per-target details at configure |
| `ENABLE_COVERAGE`      | OFF     | Enable code coverage (Debug only)     |
| `ENABLE_UPX`           | ON      | Create UPX-compressed binaries        |
| `ENABLE_CLANG_TIDY`    | OFF     | Run clang-tidy during build           |
| `PROJECT_BUILD_DOCS`   | ON      | Generate Doxygen documentation        |

### CUDA Options

| Variable                  | Default | Description                            |
| ------------------------- | ------- | -------------------------------------- |
| `CUDA_ARCHS`              | 89      | SM architectures (semicolon-separated) |
| `CUDA_KEEP_INTERMEDIATES` | ON      | Keep NVCC .ptx/.cubin files            |
| `APEX_REQUIRE_CUDA`       | OFF     | Fail if CUDA not found (cross builds)  |

### Sanitizers

```bash
cmake -DSANITIZER=asan   # Address sanitizer
cmake -DSANITIZER=tsan   # Thread sanitizer
cmake -DSANITIZER=ubsan  # Undefined behavior sanitizer
```

## Target Creation

### Libraries

```cmake
# Compiled library (STATIC or SHARED)
apex_add_library(
  NAME          mylib
  TYPE          SHARED
  INC           ${CMAKE_CURRENT_SOURCE_DIR}/inc
  SRC           src/impl.cpp
  DEPS_PUBLIC   fmt::fmt
  DEPS_PRIVATE  internal_lib
  REQUIRES      POSIX           # Default; use BAREMETAL to include on bare-metal
)

# Header-only library
apex_add_interface_library(
  NAME            mylib_headers
  INC             ${CMAKE_CURRENT_SOURCE_DIR}/inc
  DEPS_INTERFACE  fmt::fmt
)

# CUDA sidecar library
apex_add_library_cuda(
  NAME   mylib_cuda
  CORE   mylib           # Links the CPU library
  SRC    src/kernels.cu
  SEPARABLE              # Enable relocatable device code
)
```

Libraries require POSIX by default and are skipped on bare-metal builds. Add
the `BAREMETAL` flag to include a library in bare-metal builds. SHARED libraries
are automatically converted to STATIC on bare-metal.

### Apps (Production Executables)

```cmake
apex_add_app(
  NAME  myapp
  SRC   main.cpp
  LINK  mylib fmt::fmt
  # NO_UPX       # Optional: skip auto-UPX compression
)
```

Apps output to `bin/` and automatically get UPX compression enabled. Each app
is registered for release packaging via `apex_finalize_packages()`.

### Tools (Internal Utilities)

```cmake
apex_add_tool(
  NAME  mytool
  SRC   main.cpp
  LINK  mylib
)
```

Tools output to `bin/tools/`.

### Firmware (Bare-Metal Executables)

```cmake
apex_add_firmware(
  NAME          my_firmware
  MCU           STM32L476RG
  LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/linker/memory.ld
  SRC           src/main.cpp src/startup.cpp
  LINK          my_hal_lib
  INC           ${CMAKE_CURRENT_SOURCE_DIR}/inc
  DEFS          HSE_VALUE=8000000
  STACK_SIZE    0x2000
  HEAP_SIZE     0x1000
)
```

Platform-specific post-build actions are dispatched by `APEX_HAL_PLATFORM`:

| Platform | Post-Build                                 | SDK           |
| -------- | ------------------------------------------ | ------------- |
| `stm32`  | objcopy to .bin/.hex, size report          | STM32Cube HAL |
| `avr`    | objcopy to .hex, size with MCU percentages | avr-libc      |
| `pico`   | Size report (Pico SDK generates binaries)  | Pico SDK      |
| `esp32`  | Size report (ESP-IDF generates binaries)   | ESP-IDF       |

### Tests

```cmake
# Unit tests with GoogleTest
apex_add_gtest(
  TARGET   MyLib_Test
  SOURCES  test_foo.cpp test_bar.cpp
  LINK     mylib
  LABELS   Unit
)

# Performance tests (not in CTest, run manually)
apex_add_ptest(
  TARGET   MyLib_PTest
  SOURCES  perf_foo.cpp
  LINK     mylib
  TIMING_ALL            # Serialize all tests
)

# Development tests (external deps, not in CTest)
apex_add_devtest(
  TARGET   MyLib_Dev
  SOURCES  dev_foo.cpp
  LINK     mylib
)
```

Test output directories:

| Function           | Output Directory | In CTest |
| ------------------ | ---------------- | -------- |
| `apex_add_gtest`   | `bin/tests/`     | Yes      |
| `apex_add_ptest`   | `bin/ptests/`    | No       |
| `apex_add_devtest` | `bin/dtests/`    | No       |

## Code Coverage

The coverage system provides per-library HTML reports using LLVM's source-based
coverage. Coverage auto-detects project libraries from `LINK` dependencies.

### Requirements

- Clang compiler (coverage disabled with warning if GCC)
- Debug build type
- Native build (not cross-compilation)

### Enabling Coverage

Coverage is automatic for any `apex_add_gtest` that links project libraries.
Use `COVERAGE_FOR` to override the auto-detected library:

```cmake
apex_add_gtest(
  TARGET       TestMyLib
  SOURCES      mylib_test.cpp
  LINK         mylib fmt::fmt
  COVERAGE_FOR mylib          # Override: instrument only mylib
)
```

Use `NO_COVERAGE` to skip coverage entirely for a test target.

### Running Coverage

```bash
make coverage
```

This will:

1. Build with coverage instrumentation (`ENABLE_COVERAGE=ON`)
2. Run only tests labeled `Coverage` (`ctest -L Coverage`)
3. Generate per-library reports

### Output

Reports are generated in `build/<preset>/coverage/<library>/`:

| File              | Purpose                                     |
| ----------------- | ------------------------------------------- |
| `html/index.html` | Interactive HTML report (browser)           |
| `summary.txt`     | Text summary (terminal/CI logs)             |
| `lcov.info`       | LCOV format (Codecov, Coveralls, GitLab CI) |

### CUDA Limitations

Coverage only instruments host code. CUDA device kernels compiled by NVCC
are not covered:

| Code Type                | Covered?                     |
| ------------------------ | ---------------------------- |
| `.cpp` files             | Yes                          |
| Host code in `.cu` files | Yes (if Clang host compiler) |
| CUDA device kernels      | No                           |

## CUDA Integration

### Adding CUDA to Existing Targets

```cmake
apex_cuda_sources(mylib
  FILES src/kernel.cu
  SEPARABLE              # Optional: enable RDC
  RESOLVE_DEVICE_SYMBOLS # Optional: resolve at link time
  NO_CUDART              # Optional: don't auto-link cudart
)
```

### NVML and CUPTI

```cmake
# Defines COMPAT_NVML_AVAILABLE=1 or 0
apex_nvml_enable(mylib)

# Defines COMPAT_CUPTI_AVAILABLE=1 or 0
apex_cupti_enable(mylib)
```

In code:

```cpp
#if COMPAT_NVML_AVAILABLE
  nvmlInit();
#endif
```

### Environment Variables

| Variable           | Purpose                        |
| ------------------ | ------------------------------ |
| `NVML_ROOT`        | Custom NVML installation path  |
| `NVIDIA_ML_ROOT`   | Alternative to NVML_ROOT       |
| `CUDAToolkit_ROOT` | Override CUDA toolkit location |

## Data Definitions

Register `apex_data.toml` manifests for struct dictionary generation:

```cmake
apex_add_data_manifest()                          # Uses local apex_data.toml
apex_add_data_manifest(MANIFEST path/to/manifest) # Explicit path
```

Manifests are collected by `apex_finalize_data_manifests()` and written to
`apex_data_manifests.txt` in the build directory. The `make apex-data-db` target
reads this file and invokes `apex_data_gen` to produce JSON struct dictionaries.

## Packaging

Apps registered via `apex_add_app()` get `package_<APP>` custom targets created
by `apex_finalize_packages()`. Each target invokes `pkg_resolve.sh` (shell
script) to:

1. BFS-walk the binary's ELF `DT_NEEDED` entries via `readelf`
2. Resolve shared library dependencies against the build `lib/` directory
3. Stage `bin/<APP>` + `lib/*.so` (with symlink chains) into `packages/<APP>/`
4. Include TPRM configuration and generate a launch script (`run.sh`)
5. Create a deployable tarball

TPRM paths are registered per-app via `apex_set_app_tprm()`. These targets
are consumed by `mk/release.mk` for multi-platform release packaging.

## Cross-Compilation

### Jetson (aarch64)

```bash
cmake --preset jetson-aarch64-debug
```

The toolchain automatically:

- Selects aarch64 cross-compilers
- Points CUDA to aarch64 target libraries
- Uses QEMU for test discovery (if available)

### Key Variables

| Variable                        | Description                 |
| ------------------------------- | --------------------------- |
| `CMAKE_TOOLCHAIN_FILE`          | Path to toolchain file      |
| `CMAKE_SYSROOT`                 | Target system root          |
| `CMAKE_CROSSCOMPILING_EMULATOR` | QEMU path for running tests |

### Available Toolchains

| File                           | Target                     |
| ------------------------------ | -------------------------- |
| `toolchains/jetson-aarch64`    | Jetson (aarch64 + CUDA)    |
| `toolchains/rpi-aarch64`       | Raspberry Pi 4/5 (aarch64) |
| `toolchains/riscv64-linux-gnu` | RISC-V 64-bit Linux        |
| `toolchains/stm32-gcc`         | ARM Cortex-M4 (STM32L4xx)  |
| `toolchains/avr-gcc`           | AVR ATmega328P (Arduino)   |
| `toolchains/pico-gcc`          | ARM Cortex-M0+ (RP2040)    |
| `toolchains/esp32-gcc`         | Xtensa LX7 (ESP32-S3)      |
| `toolchains/zephyr`            | Zephyr RTOS (multi-arch)   |

## Output Structure

```
build/
  bin/
    MyApp           # Production apps (apex_add_app)
    MyApp.upx       # UPX-compressed copy
    tools/          # Internal tools (apex_add_tool)
    tests/          # Unit tests
    ptests/         # Performance tests
    dtests/         # Development tests
  lib/              # Shared/static libraries
  firmware/         # Bare-metal firmware (.elf, .bin, .hex)
  docs/             # Doxygen output
  nvcc/             # CUDA intermediates (if enabled)
  packages/         # Release packages (per-app bin/ + lib/)
  coverage/         # Coverage reports (per-library)
    mylib/
      html/         # Interactive HTML
      summary.txt   # Text summary
      lcov.info     # LCOV export
```

## Build Performance

The `BuildAcceleration` module auto-detects and enables optimizations:

| Feature        | Tool              | Speedup          | Auto-enabled |
| -------------- | ----------------- | ---------------- | ------------ |
| Compiler cache | ccache/sccache    | 10-100x rebuilds | Yes          |
| Fast linker    | mold > lld > gold | 2-10x link time  | Yes (native) |
| Split DWARF    | -gsplit-dwarf     | 2-5x debug link  | Yes (Debug)  |

### Acceleration Options

| Variable               | Default | Description                     |
| ---------------------- | ------- | ------------------------------- |
| `APEX_USE_CCACHE`      | ON      | Use ccache/sccache if found     |
| `APEX_USE_FAST_LINKER` | ON      | Use mold/lld/gold if found      |
| `APEX_USE_SPLIT_DWARF` | ON      | Split debug info (Debug builds) |

## Troubleshooting

### NVML Not Found

NVML requires the NVIDIA driver. On systems without a GPU:

- Cross builds: Uses stubs (link-time only, runtime requires driver)
- Native builds: Set `NVML_ROOT` to a custom location

### CUDA Wrong Architecture (Cross Build)

Ensure `CUDAToolkit_ROOT` points to the target architecture:

```bash
cmake -DCUDAToolkit_ROOT=/usr/local/cuda/targets/aarch64-linux ...
```

### Compiler Triple Mismatch

Cross builds show `[OK]`/`[FAIL]` for compiler triples in the configure summary.
A failure means the compiler targets the wrong architecture. Check
`CMAKE_C_COMPILER_TARGET`.

### Coverage Shows 0%

- Verify Clang is the compiler (GCC not supported)
- Ensure tests link project libraries (auto-detected from `LINK`)
- Check that tests actually exercise the library code
