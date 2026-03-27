# BatchStats Model

**Namespace:** `sim::gpu_compute`
**Platform:** Linux (CUDA optional)
**C++ Standard:** C++23

GPU parallel reduction model: min/max/mean/variance and histogram over large float arrays using warp-shuffle reduction.

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
#include "src/sim/gpu_compute/batch_stats/inc/BatchStatsModel.hpp"

sim::gpu_compute::BatchStatsModel model;
model.init();

// Kick launches GPU work asynchronously
model.kick();

// Poll checks completion (non-blocking)
model.poll();
```

---

## 2. Key Features

| Feature                | Detail                                                         |
| ---------------------- | -------------------------------------------------------------- |
| Warp-shuffle reduction | Per-group min/max/sum/sumSq via `__shfl_down_sync`             |
| Atomic histogram       | Per-group binning with configurable range and bin count        |
| Async kick/poll        | Non-blocking CPU-side scheduling; GPU runs on its own stream   |
| TPRM integration       | Runtime-adjustable element count, group size, histogram config |
| Graceful CPU fallback  | Compiles and runs without CUDA (no-op kernels)                 |

---

## 3. Common Workflows

### Async GPU Scheduling

```cpp
// KICK task (taskUid=1): harvest prior results, launch new work
model.kick();

// POLL task (taskUid=2): non-blocking completion check
model.poll();

// Access results after completion
const auto& STATE = model.state();
fmt::print("Min: {}, Max: {}, Mean: {}\n",
           STATE.lastMinVal, STATE.lastMaxVal, STATE.lastMeanVal);
```

### Tunable Parameter Adjustment

```cpp
sim::gpu_compute::BatchStatsTunableParams params;
params.elementCount = 1u << 24; // 16M elements
params.groupSize = 8192;
params.histogramBins = 128;
params.histogramMin = -5.0f;
params.histogramMax = 5.0f;
```

---

## 4. API Reference

### BatchStatsModel

**Header:** `BatchStatsModel.hpp`
**Component ID:** 132

| Method            | RT-Safe | Purpose                                    |
| ----------------- | ------- | ------------------------------------------ |
| `init()`          | No      | Allocate device memory, create CUDA stream |
| `kick()`          | Yes     | Harvest prior results, launch GPU kernels  |
| `poll()`          | Yes     | Non-blocking completion check              |
| `teardown()`      | No      | Free device memory                         |
| `logTprmConfig()` | No      | Log TPRM configuration to file             |

### CUDA Kernel API

**Header:** `BatchStatsKernel.cuh`
**Namespace:** `sim::gpu_compute::cuda`

| Function               | RT-Safe | Purpose                               |
| ---------------------- | ------- | ------------------------------------- |
| `batchStatsCuda()`     | Yes     | Per-group min/max/sum/sumSq reduction |
| `batchHistogramCuda()` | Yes     | Per-group histogram binning           |

### Data Structures

| Struct                    | Purpose                                                  |
| ------------------------- | -------------------------------------------------------- |
| `BatchStatsTunableParams` | Runtime config (24 bytes, static_assert verified)        |
| `BatchStatsState`         | Execution tracking (kick/complete/busy/error counts)     |
| `GroupStats`              | Per-group reduction output (min, max, sum, sumSq, count) |

---

## 5. Requirements

| Requirement  | Detail                              |
| ------------ | ----------------------------------- |
| Compiler     | C++23 + CUDA (optional)             |
| Platform     | Linux                               |
| Dependencies | system_component, compatibility     |
| GPU          | NVIDIA with CUDA runtime (optional) |

---

## 6. Testing

Run tests using the standard Docker workflow:

```bash
# Build
make compose-debug

# Run all tests
make compose-testp

# Run batch_stats tests only
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -R BatchStats
```

### Test Organization

| Module           | Test File                 | Tests |
| ---------------- | ------------------------- | ----- |
| BatchStatsModel  | BatchStatsModel_uTest.cpp | 13    |
| BatchStatsKernel | BatchStatsKernel_uTest.cu | 9     |

### Expected Output

```
100% tests passed, 0 tests failed out of 22
```

---

## 7. See Also

- `src/sim/gpu_compute/conv_filter/` -- 2D convolution model
- `src/sim/gpu_compute/fft_analyzer/` -- Batched FFT analysis model
- `src/sim/gpu_compute/stream_compact/` -- Stream compaction model
- `src/sim/gpu_compute/ptst/GpuCompute_pTest.cu` -- Shared performance tests
