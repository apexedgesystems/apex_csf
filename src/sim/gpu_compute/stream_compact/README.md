# StreamCompact Model

**Namespace:** `sim::gpu_compute`
**Platform:** Linux (CUDA optional)
**C++ Standard:** C++23

GPU stream compaction model with threshold detection, warp prefix-sum scatter, and classification histogram.

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
#include "src/sim/gpu_compute/stream_compact/inc/StreamCompactModel.hpp"

sim::gpu_compute::StreamCompactModel model;
model.init();

// Kick launches threshold + compact + classify pipeline
model.kick();

// Poll checks completion (non-blocking)
model.poll();

const auto& STATE = model.state();
fmt::print("Selected {} of {} elements ({:.1f}%)\n",
           STATE.lastCompactedCount, STATE.lastTotalCount,
           STATE.lastSelectivity * 100.0f);
```

---

## 2. Key Features

| Feature                  | Detail                                                             |
| ------------------------ | ------------------------------------------------------------------ |
| Threshold selection      | Elements >= threshold are selected for output                      |
| Warp prefix-sum          | Intra-warp inclusive scan via `__shfl_up_sync` for scatter indices |
| Atomic global count      | Cross-warp coordination with `atomicAdd`                           |
| Classification histogram | Bin compacted elements into configurable class ranges              |
| Async kick/poll          | Non-blocking CPU-side scheduling                                   |

---

## 3. Common Workflows

### Detection Pipeline

```cpp
sim::gpu_compute::StreamCompactTunableParams params;
params.fieldWidth = 4096;
params.fieldHeight = 4096;
params.threshold = 0.7f;   // Select strong detections
params.classCount = 16;
params.classMin = 0.0f;
params.classMax = 1.0f;

// Pipeline:
// 1. Threshold: mark elements >= 0.7
// 2. Compact: prefix-sum scatter to contiguous output
// 3. Classify: bin compacted elements into 16 classes
```

---

## 4. API Reference

### StreamCompactModel

**Header:** `StreamCompactModel.hpp`
**Component ID:** 133

| Method       | RT-Safe | Purpose                                    |
| ------------ | ------- | ------------------------------------------ |
| `init()`     | No      | Allocate device memory, create CUDA stream |
| `kick()`     | Yes     | Launch compact + classify pipeline         |
| `poll()`     | Yes     | Non-blocking completion check              |
| `teardown()` | No      | Free device memory                         |

### CUDA Kernel API

**Header:** `StreamCompactKernel.cuh`
**Namespace:** `sim::gpu_compute::cuda`

| Function                  | RT-Safe | Purpose                                |
| ------------------------- | ------- | -------------------------------------- |
| `streamCompactCuda()`     | Yes     | Threshold + warp prefix-sum compaction |
| `classifyHistogramCuda()` | Yes     | Bin compacted elements into histogram  |

### Data Structures

| Struct                       | Purpose                                           |
| ---------------------------- | ------------------------------------------------- |
| `StreamCompactTunableParams` | Runtime config (24 bytes, static_assert verified) |
| `StreamCompactState`         | Execution tracking + selectivity metrics          |

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

# Run stream_compact tests only
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -R StreamCompact
```

### Test Organization

| Module              | Test File                    | Tests |
| ------------------- | ---------------------------- | ----- |
| StreamCompactModel  | StreamCompactModel_uTest.cpp | 10    |
| StreamCompactKernel | StreamCompactKernel_uTest.cu | 7     |

### Expected Output

```
100% tests passed, 0 tests failed out of 17
```

---

## 7. See Also

- `src/sim/gpu_compute/batch_stats/` -- Parallel reduction model
- `src/sim/gpu_compute/conv_filter/` -- 2D convolution model
- `src/sim/gpu_compute/fft_analyzer/` -- Batched FFT analysis model
- `src/sim/gpu_compute/ptst/GpuCompute_pTest.cu` -- Shared performance tests
