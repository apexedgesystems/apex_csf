# SystemMonitor

**Namespace:** `system_core::support`
**Platform:** Linux (POSIX)
**C++ Standard:** C++23

Runtime system health monitor with configurable threshold alerting across
CPU, memory, and GPU domains.

---

## Table of Contents

1. [Quick Start](#1-quick-start)
2. [Key Features](#2-key-features)
3. [Common Workflows](#3-common-workflows)
4. [API Reference](#4-api-reference)
5. [Requirements](#5-requirements)
6. [Testing](#6-testing)
7. [See Also](#7-see-also)

---

## 1. Quick Start

```cpp
#include "src/system/core/support/system_monitor/inc/SystemMonitor.hpp"

system_core::support::SystemMonitor monitor;

// Override defaults (optional)
system_core::support::SystemMonitorConfig cfg;
cfg.cpu.monitoredCores[0] = 0;
cfg.cpu.monitoredCores[1] = 2;
cfg.cpu.monitoredCoreCount = 2;
cfg.gpu.enabled = false;
monitor.setConfig(cfg);

// Register with executive (executive calls init + schedules telemetry)
executive.registerSupport(&monitor);
```

---

## 2. Key Features

### Two-Phase Monitoring

1. **Init Snapshot** (NOT RT-safe, runs once): Captures static system
   properties -- kernel version, RT config, CPU topology, memory limits,
   GPU info. Logged at startup.

2. **Runtime Telemetry** (RT-safe, periodic task): Samples per-core CPU
   utilization, CPU temperature, RAM/swap usage, FD count, RT throttle
   events, and optionally GPU telemetry. Compares against configurable
   warn/crit thresholds.

### Three Monitoring Domains

| Domain | Metrics                                    | Data Sources                                             |
| ------ | ------------------------------------------ | -------------------------------------------------------- |
| CPU    | Per-core load, temperature, RT throttle    | `/proc/stat`, `/sys/class/thermal/`, `/proc/sys/kernel/` |
| Memory | RAM utilization, swap usage, FD count      | `/proc/meminfo`, `/proc/self/fd`, `getrlimit()`          |
| GPU    | Temperature, utilization, memory, throttle | NVML (CUDA builds only)                                  |

Each domain is independently enabled/disabled via configuration.

### Threshold Alerting

Two severity levels per metric:

- **Warn**: Logs a warning, increments `warnCount()`
- **Crit**: Logs an error, increments `critCount()`

### TPRM Integration

All thresholds are loadable from TPRM binary files, enabling runtime
reconfiguration without recompilation.

### Structured Telemetry

Posts packed telemetry structs via the internal bus at each sample interval.
The Interface component wraps these in APROTO for external TCP clients.

---

## 3. Common Workflows

### Standalone Usage (Without Executive)

```cpp
system_core::support::SystemMonitor monitor;
monitor.setInstanceIndex(0);

system_core::support::SystemMonitorConfig cfg;
cfg.cpu.monitoredCores[0] = 0;
cfg.cpu.monitoredCoreCount = 1;
cfg.gpu.enabled = false;
monitor.setConfig(cfg);
monitor.init();

// Periodic sampling (call at configured rate)
std::this_thread::sleep_for(std::chrono::milliseconds(100));
monitor.telemetry();

// Query results
fmt::print("CPU load: {:.1f}%\n", monitor.lastCpuLoad(0));
fmt::print("RAM: {:.1f}%\n", monitor.lastRamUsedPercent());
fmt::print("FDs: {}\n", monitor.lastFdCount());
fmt::print("Warns: {} Crits: {}\n", monitor.warnCount(), monitor.critCount());
```

### Custom Thresholds for RT Systems

```cpp
system_core::support::SystemMonitorConfig cfg;

// Tight CPU thresholds for RT-critical cores
cfg.cpu.loadWarnPercent = 60.0F;
cfg.cpu.loadCritPercent = 80.0F;
cfg.cpu.tempWarnC = 70;
cfg.cpu.tempCritC = 80;

// Any swap usage is critical on RT systems
cfg.memory.swapUsedWarnBytes = 1;
cfg.memory.swapUsedCritBytes = 1;

// FD exhaustion early warning
cfg.memory.fdUsedWarnPercent = 50.0F;
cfg.memory.fdUsedCritPercent = 75.0F;

monitor.setConfig(cfg);
```

### Init Snapshot Inspection

```cpp
monitor.init();
const auto& SNAP = monitor.snapshot();

fmt::print("Kernel: {} RT={}\n",
           SNAP.kernel.release.data(),
           SNAP.kernel.isRtKernel ? "yes" : "no");
fmt::print("CPU: {} cores, clock={}\n",
           SNAP.cpu.coreCount,
           SNAP.cpu.clockSource.data());
fmt::print("RAM: {:.1f} GB\n",
           static_cast<double>(SNAP.memory.totalRamBytes) / (1024.0 * 1024.0 * 1024.0));
```

---

## 4. API Reference

### SystemMonitor

**RT-safe:** `telemetry()` and all accessors are RT-safe. `init()`, `setConfig()`,
`loadTprm()` are NOT RT-safe.

| Method                 | RT-Safe | Description                                  |
| ---------------------- | ------- | -------------------------------------------- |
| `setConfig(cfg)`       | No      | Set thresholds and domain enable flags       |
| `config()`             | Yes     | Get current configuration                    |
| `loadTprm(dir)`        | No      | Load configuration from TPRM binary          |
| `init()`               | No      | Capture system snapshot, allocate state      |
| `telemetry()`          | Yes     | Sample all enabled domains, check thresholds |
| `snapshot()`           | Yes     | Get init-time system snapshot                |
| `warnCount()`          | Yes     | Total warning threshold breaches             |
| `critCount()`          | Yes     | Total critical threshold breaches            |
| `sampleCount()`        | Yes     | Total telemetry samples taken                |
| `lastCpuLoad(i)`       | Yes     | Per-core CPU load (0-100%)                   |
| `lastCpuTempC()`       | Yes     | CPU temperature (Celsius, -1 = N/A)          |
| `lastRamUsedPercent()` | Yes     | RAM utilization (0-100%)                     |
| `lastSwapUsedBytes()`  | Yes     | Swap usage (bytes)                           |
| `lastFdCount()`        | Yes     | Open file descriptor count                   |
| `lastGpuTempC()`       | Yes     | GPU temperature (Celsius, -1 = N/A)          |
| `lastGpuUtilPercent()` | Yes     | GPU utilization (0-100%, -1 = N/A)           |

### SystemMonitorConfig

**RT-safe:** Yes (pure data structure)

Top-level configuration with per-domain sub-structs:

| Field          | Type             | Default  | Description          |
| -------------- | ---------------- | -------- | -------------------- |
| `sampleRateHz` | float            | 1.0      | Telemetry task rate  |
| `cpu`          | CpuThresholds    | enabled  | CPU domain config    |
| `memory`       | MemoryThresholds | enabled  | Memory domain config |
| `gpu`          | GpuThresholds    | disabled | GPU domain config    |

### SystemMonitorSnapshot

**RT-safe:** Yes (pure data structure, populated once at init)

| Sub-struct | Key Fields                                                               |
| ---------- | ------------------------------------------------------------------------ |
| `kernel`   | release, preemptModel, isRtKernel, nohzFull, isolCpus, highResTimers     |
| `cpu`      | coreCount, modelName, clockSource, rtBandwidthPercent                    |
| `memory`   | totalRamBytes, totalSwapBytes, swappiness, memlockSoftBytes, fdSoftLimit |
| `gpu`      | available, deviceName, computeCap, totalMemoryBytes, shutdownTempC       |

### SysMonHealthTlm

**RT-safe:** Yes (88-byte packed POD)

Telemetry wire format posted via internal bus at each sample. Contains
sample/warn/crit counters, per-core load, CPU temperature, RAM utilization,
and FD count.

---

## 5. Requirements

### Dependencies

| Dependency                     | Type               | Purpose                          |
| ------------------------------ | ------------------ | -------------------------------- |
| `system_core_system_component` | Public             | SupportComponentBase, IComponent |
| `seeker_cpu`                   | Private            | CPU utilization and topology     |
| `seeker_memory`                | Private            | Memory statistics                |
| `seeker_system`                | Private            | Kernel info, process limits      |
| `seeker_timing`                | Private            | Clock source, timer config       |
| `seeker_helpers`               | Private            | RT-safe file reads               |
| `seeker_gpu`                   | Private (optional) | GPU telemetry (CUDA builds)      |
| `utilities_helpers`            | Private            | TPRM binary loading              |
| `fmt`                          | Private            | Log formatting                   |

### Platform

- Linux only (reads `/proc/`, `/sys/`)
- Optional NVIDIA GPU support via NVML (requires CUDA toolkit)

---

## 6. Testing

```bash
# Build
make compose-debug

# Run all tests
make compose-testp

# Run SystemMonitor tests only
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -L support
```

### Test Organization

| Test File               | Tests | Coverage                            |
| ----------------------- | ----- | ----------------------------------- |
| SystemMonitor_uTest.cpp | 22    | CPU/memory/threshold/accessor paths |

Tests run on the host and exercise real `/proc` and `/sys` reads. They
verify invariants (core count > 0, load in 0-100%) rather than exact values.
GPU paths are not covered (requires NVIDIA hardware).

---

## 7. See Also

- `src/system/core/infrastructure/system_component/` - SupportComponentBase
- `src/system/core/executive/apex/` - ApexExecutive (registers and schedules monitor)
- `src/system/core/infrastructure/protocols/aproto/` - APROTO telemetry transport
