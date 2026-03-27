# Apex CSF

**Platform:** Linux (full), bare-metal MCU (embedded subset)
**C++ Standard:** C++23 (host), C++20 (bare-metal)
**Version:** 0.0.1
**License:** MIT

Apex CSF is a real-time control and simulation framework. You write
components -- models, drivers, support services -- and the framework handles
scheduling, lifecycle management, communication, logging, and deployment.
It targets the same problem space as NASA cFS and JPL F Prime but is built
on modern C++ with first-class support for CUDA, Monte Carlo simulation,
and bare-metal microcontrollers.

Use it to build flight software, hardware-in-the-loop test rigs, industrial
controllers, edge compute pipelines, or any system where deterministic
real-time scheduling matters.

---

## Table of Contents

1. [What You Build](#1-what-you-build)
2. [How It Works](#2-how-it-works)
3. [Quick Start](#3-quick-start)
4. [Where to Go Next](#4-where-to-go-next)
5. [What the Framework Provides](#5-what-the-framework-provides)
6. [Platform Support](#6-platform-support)
7. [Requirements](#7-requirements)
8. [Testing](#8-testing)
9. [Library Catalog](#9-library-catalog)
10. [Documentation](#10-documentation)
11. [License](#11-license)

---

## 1. What You Build

You write **components**. A component is a class that inherits from a
framework base, declares an identity, registers tasks, and implements those
tasks. The framework calls your tasks at the frequencies you configure.

Here is a minimal simulation model:

```cpp
#include "src/system/core/infrastructure/system_component/apex/inc/SwModelBase.hpp"

class ThermalModel final : public system_core::system_component::SwModelBase {
public:
  static constexpr std::uint16_t COMPONENT_ID   = 101;
  static constexpr const char*   COMPONENT_NAME = "ThermalModel";

  [[nodiscard]] std::uint16_t componentId()   const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char*   componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char*   label()         const noexcept override { return "THERMAL"; }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    registerTask<ThermalModel, &ThermalModel::step>(1, this, "thermalStep");
    return 0;
  }

public:
  std::uint8_t step() noexcept {
    // Your RT-safe logic here. Called every frame by the scheduler.
    temperature_ += heatInput_ * dt_ - dissipation_ * (temperature_ - ambient_);
    return 0;
  }

private:
  double temperature_{293.15};
  double heatInput_{0.0};
  double dissipation_{0.01};
  double ambient_{293.15};
  double dt_{0.01};
};
```

You then wire components into an **executive** -- a subclass of
`ApexExecutive` that owns your components and registers them at startup:

```cpp
#include "src/system/core/executive/inc/ApexExecutive.hpp"

class MyExecutive final : public executive::ApexExecutive {
public:
  using ApexExecutive::ApexExecutive;
  [[nodiscard]] const char* label() const noexcept override { return "MY_EXEC"; }

protected:
  [[nodiscard]] bool registerComponents() noexcept override {
    return registerComponent(&thermal_, fileSystem().logDir());
  }

private:
  ThermalModel thermal_;
};
```

And run it:

```cpp
int main(int argc, char* argv[]) {
  std::filesystem::path rootfs(".apex_fs");
  std::filesystem::create_directories(rootfs);

  MyExecutive exec(argv[0], {}, rootfs);
  if (exec.init() != 0) { return 1; }
  return exec.run();
}
```

That is the entire pattern. The executive initializes the scheduler, registry,
filesystem, and logging. Your component's `doInit()` registers tasks. The
scheduler calls those tasks at the configured frequency. Everything in the
hot path is RT-safe: no allocations, no blocking, bounded execution.

For bare-metal targets, the same pattern applies with `LiteExecutive`,
`LiteComponentBase`, and `SchedulerLite<N>` -- zero heap allocation.

---

## 2. How It Works

```
    +------------------------------------------------------+
    |                    ApexExecutive                      |
    +------------------------------------------------------+
    |                                                      |
    |   +-----------+   +----------+   +--------------+    |
    |   | Scheduler |   | Registry |   |  FileSystem  |    |
    |   +-----------+   +----------+   +--------------+    |
    |                                                      |
    +------------------------------------------------------+
           |                  |
           | schedules        | tracks identity, tasks, data
           v                  v
    +------------------------------------------------------+
    |              Your Components                         |
    |                                                      |
    |   ThermalModel    DriverModel    SensorModel   ...   |
    |     .step()         .read()        .sample()         |
    +------------------------------------------------------+
           |                                  |
           v                                  v
    +-----------------+              +-----------------+
    |    Protocols    |              |   Data / Logs   |
    |  (I/O to world) |              | (state capture) |
    +-----------------+              +-----------------+
```

**Lifecycle has three phases:**

1. **Registration** -- The executive assigns each component a unique ID,
   components register their tasks and data blocks with the scheduler and
   registry. Vectors grow. Not RT-safe.
2. **Freeze** -- The registry locks. All queries become RT-safe (fixed-size,
   no allocation). No further registration.
3. **Execution** -- The scheduler ticks at a fundamental frequency. Each task
   fires based on its N/D frequency ratio and phase offset. All hot-path
   operations are RT-safe.

The **scheduler** supports three execution models:

- `SchedulerMultiThread` -- parallel execution with a thread pool (Linux)
- `SchedulerSingleThread` -- deterministic sequential execution (Linux)
- `SchedulerLite<N>` -- static task table, zero heap (bare-metal MCU)

Task frequency, priority, phase, and thread affinity are configured externally
via **TPRM** (tunable parameter) files -- not hard-coded. Change timing without
recompiling.

---

## 3. Quick Start

### Build and Test (Docker, recommended)

```bash
make compose-debug
make compose-testp
```

### Build Without Docker

```bash
cmake --preset native-linux-debug
cmake --build --preset native-linux-debug -j$(nproc)
ctest --test-dir build/native-linux-debug --output-on-failure
```

### Cross-Compile

```bash
make compose-jetson-release    # NVIDIA Jetson (aarch64)
make compose-rpi-release       # Raspberry Pi (aarch64)
make compose-riscv-release     # RISC-V 64
```

### Firmware

```bash
make compose-stm32                                          # Build
make compose-stm32-flash STM32_FIRMWARE=stm32_encryptor     # Flash
```

Run `make help` for the full list of targets.

---

## 4. Where to Go Next

After building, start with one of these depending on your interest:

| If you want to...                       | Start here                                                                                |
| --------------------------------------- | ----------------------------------------------------------------------------------------- |
| Run a full executive with GPU models    | [apex_edge_demo](apps/apex_edge_demo/)                                                    |
| See host + MCU hardware-in-the-loop     | [apex_hil_demo](apps/apex_hil_demo/)                                                      |
| Run Monte Carlo sweeps across all cores | [apex_mc_demo](apps/apex_mc_demo/)                                                        |
| Deploy AES-256-GCM on a microcontroller | [stm32_encryptor_demo](apps/stm32_encryptor_demo/)                                        |
| Understand the component base classes   | [system_component README](src/system/core/infrastructure/system_component/base/README.md) |
| Understand task scheduling              | [scheduler README](src/system/core/components/scheduler/README.md)                        |
| Browse the full protocol stack          | [Protocols](#protocols) in the library catalog below                                      |

### Demo Applications

| Application                                            | Description                                                   | Platforms          |
| ------------------------------------------------------ | ------------------------------------------------------------- | ------------------ |
| [apex_edge_demo](apps/apex_edge_demo/)                 | GPU workloads under ApexExecutive RT scheduling               | x86_64 + CUDA      |
| [apex_hil_demo](apps/apex_hil_demo/)                   | POSIX plant model + STM32 flight controller over UART/SLIP    | x86_64 + STM32     |
| [apex_mc_demo](apps/apex_mc_demo/)                     | Monte Carlo voltage regulator tolerance analysis              | x86_64             |
| [stm32_encryptor_demo](apps/stm32_encryptor_demo/)     | AES-256-GCM encryption with dual UART channels                | NUCLEO-L476RG      |
| [arduino_encryptor_demo](apps/arduino_encryptor_demo/) | AES-256-GCM encryption on ATmega328P (32 KB flash, 2 KB SRAM) | Arduino Uno R3     |
| [pico_encryptor_demo](apps/pico_encryptor_demo/)       | AES-256-GCM encryption with dual UART                         | Raspberry Pi Pico  |
| [esp32_encryptor_demo](apps/esp32_encryptor_demo/)     | AES-256-GCM encryption with UART + USB CDC                    | Arduino Nano ESP32 |
| [c2000_encryptor_demo](apps/c2000_encryptor_demo/)     | AES-256-GCM + CAN loopback                                    | LAUNCHXL-F280049C  |

---

## 5. What the Framework Provides

### Real-Time Executive

Deterministic scheduling with component lifecycle management, tunable
parameters (TPRM) with runtime reload, and a unified registry for identity,
tasks, and data. Runs on Linux (multi-thread or single-thread) and bare-metal
MCUs (static task table, zero heap).

### Communication Protocols

| Domain    | Protocols                                                  |
| --------- | ---------------------------------------------------------- |
| Aerospace | CCSDS Space Packet (SPP), CCSDS Encapsulation Packet (EPP) |
| Network   | TCP, UDP, Unix sockets (epoll-based)                       |
| Fieldbus  | CAN, LIN, Modbus                                           |
| Serial    | UART, SPI, I2C                                             |
| Wireless  | Bluetooth                                                  |
| Framing   | COBS, SLIP (zero-allocation)                               |
| Custom    | APROTO (command/telemetry with optional AEAD encryption)   |

### Cryptography

AES-128/256 (CBC, CTR, GCM, CCM), ChaCha20-Poly1305, SHA-256/512, BLAKE2s,
SHA3-256, HMAC, CMAC, Poly1305, HKDF. All APIs have zero-allocation variants
for RT-safe use. Bare-metal subset: AES-256-GCM.

### Simulation

Spherical harmonic gravity (EGM2008 degree 2190, GRGM1200A degree 1200),
circuit simulation (MNA, transient), ODE integrators, quaternion math, and
Monte Carlo batch execution across a thread pool.

### GPU Compute (CUDA)

Batched FFT, parallel statistics/reduction, 2D convolution, stream compaction,
and NVML telemetry. Integrated into the executive via async kick/poll tasks
for non-blocking GPU dispatch from the RT loop.

### Math

Linear algebra (non-owning views, optional BLAS/LAPACK/CUDA), ODE integrators
(explicit and implicit, CRTP zero-allocation), Legendre functions, and unit
quaternions with optional GPU batch acceleration.

### Build and Tooling

Docker images for 13 platforms with pre-configured toolchains. Unified Make
interface. CMake infrastructure for libraries, tests, coverage, firmware, and
packaging. Rust CLI tools for TPRM management, data generation, and hardware
testing.

---

## 6. Platform Support

### Host Targets

| Platform               | Build  | Test | CUDA | Pre-built Artifact |
| ---------------------- | ------ | ---- | ---- | ------------------ |
| x86_64 Linux           | Native | Yes  | Yes  | cpu, cuda          |
| Jetson (aarch64)       | Cross  | No   | Yes  | jetson             |
| Raspberry Pi (aarch64) | Cross  | No   | No   | rpi                |
| RISC-V 64              | Cross  | No   | No   | riscv64            |

### Bare-Metal Targets

| Platform    | MCU/Arch       | Compiler              | Flash Tool |
| ----------- | -------------- | --------------------- | ---------- |
| STM32       | ARM Cortex-M4  | arm-none-eabi-gcc     | st-flash   |
| Arduino     | AVR ATmega328P | avr-gcc (Arduino CLI) | avrdude    |
| Pico        | ARM Cortex-M0+ | arm-none-eabi-gcc     | picotool   |
| ESP32       | Xtensa LX7     | xtensa-esp32s3-elf    | esptool    |
| ATmega328PB | AVR            | avr-gcc               | avrdude    |
| C2000       | TI C28x DSP    | cl2000                | UniFlash   |

All toolchains are pre-configured in Docker images. See
[docker/README.md](docker/README.md) for the image hierarchy.

---

## 7. Requirements

**Required:**

- C++23 compiler (Clang 21 recommended)
- CMake 3.24+
- Ninja build system

**Optional:**

- CUDA toolkit 13+ (GPU features)
- OpenSSL (encryption library)
- SuiteSparse (KLU sparse solver, circuit simulation)
- Rust toolchain (CLI tools)
- Python 3.10+ with Poetry (CLI tools)

All dependencies are available in the provided Docker images.

---

## 8. Testing

```bash
# Build and run all tests (Docker, recommended)
make compose-debug
make compose-testp

# CLI tool tests
make test-rust
make test-py

# Code coverage (reports: build/native-linux-debug/coverage/*/html/index.html)
make compose-coverage

# Sanitizers
make compose-asan     # Address sanitizer
make compose-tsan     # Thread sanitizer
make compose-ubsan    # Undefined behavior sanitizer
```

Performance benchmarking uses
[Vernier](https://github.com/apexedgesystems/vernier).

---

## 9. Library Catalog

### Utilities

| Library                                                | Namespace           | Description                                                               |
| ------------------------------------------------------ | ------------------- | ------------------------------------------------------------------------- |
| [checksums](src/utilities/checksums/)                  | `apex::checksums`   | CRC algorithms with hardware acceleration (SSE4.2, ARM CRC32)             |
| [compatibility](src/utilities/compatibility/README.md) | `apex::compat`      | Header-only shims for C++ standards, OpenSSL, and GPU builds              |
| [concurrency](src/utilities/concurrency/README.md)     | `apex::concurrency` | Lock-free primitives and synchronization for real-time systems            |
| [encryption](src/utilities/encryption/README.md)       | `apex::encryption`  | AES, ChaCha20-Poly1305, SHA-256/512, HMAC, HKDF with zero-allocation APIs |
| [helpers](src/utilities/helpers/README.md)             | `apex::helpers`     | Bit manipulation, byte ordering, CPU primitives, socket utilities         |
| [time](src/utilities/time/README.md)                   | `apex::time`        | Time standards, clock providers, and conversion utilities                 |

### Math

| Library                                                 | Namespace                 | Description                                                                   |
| ------------------------------------------------------- | ------------------------- | ----------------------------------------------------------------------------- |
| [integration](src/utilities/math/integration/README.md) | `apex::math::integration` | ODE integrators (explicit/implicit) with CRTP zero-allocation design          |
| [legendre](src/utilities/math/legendre/README.md)       | `apex::math::legendre`    | Fully normalized associated Legendre functions with optional GPU acceleration |
| [linalg](src/utilities/math/linalg/README.md)           | `apex::math::linalg`      | Non-owning 2D array views with optional BLAS/LAPACK and CUDA batch processing |
| [quaternion](src/utilities/math/quaternion/README.md)   | `apex::math::quaternion`  | Unit quaternion operations for 3D rotations with optional GPU acceleration    |

### System Core -- Components

| Library                                                       | Namespace                 | Description                                                               |
| ------------------------------------------------------------- | ------------------------- | ------------------------------------------------------------------------- |
| [executive](src/system/core/executive/README.md)              | `system_core`             | Central coordinator: owns scheduler, registry, filesystem                 |
| [scheduler](src/system/core/components/scheduler/README.md)   | `system_core::scheduler`  | RT task scheduling with N/D frequency decimation and phase sequencing     |
| [registry](src/system/core/components/registry/README.md)     | `system_core::registry`   | Centralized tracking of components, tasks, and data with RT-safe queries  |
| [filesystem](src/system/core/components/filesystem/README.md) | `system_core::filesystem` | Deterministic no-throw file management with atomic writes and archival    |
| [action](src/system/core/components/action/README.md)         | `system_core::action`     | Command sequencing engine for onboard execution (RTS/ATS)                 |
| [interface](src/system/core/components/interface/README.md)   | `system_core::interface`  | Command/telemetry interface over TCP with APROTO and configurable framing |

### System Core -- Infrastructure

| Library                                                                            | Namespace                       | Description                                                                               |
| ---------------------------------------------------------------------------------- | ------------------------------- | ----------------------------------------------------------------------------------------- |
| [system_component](src/system/core/infrastructure/system_component/base/README.md) | `system_core::system_component` | Component base classes and lifecycle (IComponent, SystemComponentBase, LiteComponentBase) |
| [schedulable](src/system/core/infrastructure/schedulable/README.md)                | `system_core::schedulable`      | Lightweight task abstractions (~24 bytes) with zero-cost delegates                        |
| [data](src/system/core/infrastructure/data/README.md)                              | `system_core::data`             | Typed data containers, endianness handling, watchpoints, and fault injection              |
| [logs](src/system/core/infrastructure/logs/README.md)                              | `logs`                          | RT-safe asynchronous logging with file-backed persistence and rotation                    |

### Protocols

| Library                                                                   | Namespace                                | Description                                                             |
| ------------------------------------------------------------------------- | ---------------------------------------- | ----------------------------------------------------------------------- |
| [aproto](src/system/core/infrastructure/protocols/aproto/README.md)       | `system_core::protocols::aproto`         | Binary command/telemetry protocol with optional CRC and AEAD encryption |
| [ccsds_spp](src/system/core/infrastructure/protocols/ccsds/spp/README.md) | `protocols::ccsds::spp`                  | CCSDS Space Packet Protocol (CCSDS 133.0-B-2)                           |
| [ccsds_epp](src/system/core/infrastructure/protocols/ccsds/epp/README.md) | `protocols::ccsds::epp`                  | CCSDS Encapsulation Packet Protocol (CCSDS 133.1-B-3)                   |
| [framing](src/system/core/infrastructure/protocols/framing/README.md)     | `apex::protocols::{slip,cobs}`           | Zero-allocation COBS and SLIP framing for byte streams                  |
| [fieldbus](src/system/core/infrastructure/protocols/fieldbus/README.md)   | `apex::protocols::fieldbus`              | CAN, LIN, and Modbus with RT-safe I/O paths                             |
| [network](src/system/core/infrastructure/protocols/network/README.md)     | `apex::protocols::{tcp,udp,unix_socket}` | TCP, UDP, Unix sockets with epoll-based event loop                      |
| [common](src/system/core/infrastructure/protocols/common/README.md)       | `apex::protocols`                        | Byte-level I/O tracing and formatting mixins                            |

### Hardware Abstraction

| Library                              | Namespace   | Description                                                                      |
| ------------------------------------ | ----------- | -------------------------------------------------------------------------------- |
| [hal](src/system/core/hal/README.md) | `apex::hal` | Platform-agnostic UART, CAN, SPI, I2C, Flash interfaces with zero-allocation I/O |

### Simulation

| Library                                              | Namespace                   | Description                                                                       |
| ---------------------------------------------------- | --------------------------- | --------------------------------------------------------------------------------- |
| [monte_carlo](src/system/core/monte_carlo/README.md) | `apex::monte_carlo`         | Header-only batch Monte Carlo execution across a thread pool                      |
| [gravity](src/sim/environment/gravity/README.md)     | `sim::environment::gravity` | Spherical harmonic gravity (EGM2008 degree 2190, GRGM1200A degree 1200) with CUDA |
| [regulator](src/sim/analog/regulator/README.md)      | `sim::analog`               | LDO voltage regulator model for Monte Carlo tolerance analysis                    |

### GPU Compute (CUDA)

| Library                                                        | Namespace          | Description                                                              |
| -------------------------------------------------------------- | ------------------ | ------------------------------------------------------------------------ |
| [batch_stats](src/sim/gpu_compute/batch_stats/README.md)       | `sim::gpu_compute` | Parallel reduction: min/max/mean/variance and histogram via warp-shuffle |
| [conv_filter](src/sim/gpu_compute/conv_filter/README.md)       | `sim::gpu_compute` | 2D convolution with shared memory tiling and halo exchange               |
| [fft_analyzer](src/sim/gpu_compute/fft_analyzer/README.md)     | `sim::gpu_compute` | Batched R2C FFT with spectral analysis via cuFFT                         |
| [stream_compact](src/sim/gpu_compute/stream_compact/README.md) | `sim::gpu_compute` | Threshold detection and warp prefix-sum scatter                          |

### Tools

| Tool                 | Purpose                                          |
| -------------------- | ------------------------------------------------ |
| `cfg2bin`            | Convert configuration files to binary            |
| `tprm_template`      | Generate TOML templates for tunable parameters   |
| `tprm_pack`          | Pack/unpack TPRM archives                        |
| `apex_data_gen`      | Generate struct dictionaries from JSON manifests |
| `rdat_tool`          | Analyze registry RDAT files                      |
| `sdat_tool`          | Analyze scheduler SDAT files                     |
| `serial_dev_checker` | Scan serial device status                        |
| `serial_dev_tester`  | Serial loopback verification                     |
| `upx_tool`           | UPX compression/decompression helper             |

See [tools/rust/README.md](tools/rust/README.md),
[tools/py/README.md](tools/py/README.md),
[tools/cpp/README.md](tools/cpp/README.md), and
[tools/sh/README.md](tools/sh/README.md) for full documentation.

---

## 10. Documentation

### Build System

| Document                                     | Purpose                          |
| -------------------------------------------- | -------------------------------- |
| [cmake/apex/README.md](cmake/apex/README.md) | CMake module reference and API   |
| [mk/README.md](mk/README.md)                 | Make targets and configuration   |
| [docker/README.md](docker/README.md)         | Docker image hierarchy and usage |

### Related Projects

| Project                                               | Purpose                  |
| ----------------------------------------------------- | ------------------------ |
| [Vernier](https://github.com/apexedgesystems/vernier) | Performance benchmarking |
| [Seeker](https://github.com/apexedgesystems/seeker)   | System diagnostics       |

---

## 11. License

MIT License. See [LICENSE](LICENSE) for details.
