# ConvFilter Model

**Namespace:** `sim::gpu_compute`
**Platform:** Linux (CUDA optional)
**C++ Standard:** C++23

GPU 2D convolution model with shared memory tiling, halo exchange, and constant memory kernel weights.

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
#include "src/sim/gpu_compute/conv_filter/inc/ConvFilterModel.hpp"

sim::gpu_compute::ConvFilterModel model;
model.init();

// Kick launches convolution asynchronously
model.kick();

// Poll checks completion (non-blocking)
model.poll();
```

---

## 2. Key Features

| Feature                 | Detail                                                                  |
| ----------------------- | ----------------------------------------------------------------------- |
| Shared memory tiling    | Tile + halo loaded cooperatively, convolution on interior               |
| Constant memory kernels | Weights broadcast from `__constant__` for fast access                   |
| Separable decomposition | Two-pass 1D (H then V) for separable kernels: 2\*(2R+1) vs (2R+1)^2 ops |
| Kernel generators       | Host-side Gaussian, box kernel generation with normalization            |
| Max radius 15           | Supports up to 31x31 convolution kernels                                |
| Async kick/poll         | Non-blocking CPU-side scheduling                                        |

---

## 3. Common Workflows

### 2D Convolution

```cpp
// Upload Gaussian kernel weights to constant memory
std::vector<float> kernel((2 * radius + 1) * (2 * radius + 1));
sim::gpu_compute::cuda::generateGaussianKernel(kernel.data(), radius, sigma);
sim::gpu_compute::cuda::convSetKernel(kernel.data(), radius);

// Launch convolution
sim::gpu_compute::cuda::conv2dCuda(dInput, width, height, radius, dOutput, stream);
```

### Separable Two-Pass (faster for large radii)

```cpp
// Upload 1D kernel
std::vector<float> kernel1D(2 * radius + 1);
sim::gpu_compute::cuda::generateGaussianKernel1D(kernel1D.data(), radius, sigma);
sim::gpu_compute::cuda::convSetKernel1D(kernel1D.data(), radius);

// Two-pass: horizontal then vertical
sim::gpu_compute::cuda::conv2dSeparableCuda(dInput, width, height, radius, dTemp, dOutput, stream);
```

---

## 4. API Reference

### ConvFilterModel

**Header:** `ConvFilterModel.hpp`
**Component ID:** 130

| Method       | RT-Safe | Purpose                                             |
| ------------ | ------- | --------------------------------------------------- |
| `init()`     | No      | Allocate device memory, create CUDA stream          |
| `kick()`     | Yes     | Generate kernel, upload weights, launch convolution |
| `poll()`     | Yes     | Non-blocking completion check                       |
| `teardown()` | No      | Free device memory                                  |

### CUDA Kernel API

**Header:** `ConvFilterKernel.cuh`
**Namespace:** `sim::gpu_compute::cuda`

| Function                     | RT-Safe | Purpose                                  |
| ---------------------------- | ------- | ---------------------------------------- |
| `convSetKernel()`            | No      | Upload 2D weights to constant memory     |
| `conv2dCuda()`               | Yes     | 2D tiled convolution                     |
| `convSetKernel1D()`          | No      | Upload 1D weights to constant memory     |
| `conv2dSeparableCuda()`      | Yes     | Two-pass separable convolution           |
| `generateGaussianKernel()`   | No      | Generate 2D Gaussian weights on host     |
| `generateGaussianKernel1D()` | No      | Generate 1D Gaussian weights on host     |
| `generateBoxKernel()`        | No      | Generate box (averaging) weights on host |

### Data Structures

| Struct                    | Purpose                                              |
| ------------------------- | ---------------------------------------------------- |
| `ConvFilterTunableParams` | Runtime config (24 bytes, static_assert verified)    |
| `ConvFilterState`         | Execution tracking (kick/complete/busy/error counts) |
| `CONV_MAX_KERNEL_RADIUS`  | Max supported radius: 15 (31x31 kernel)              |

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

# Run conv_filter tests only
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -R ConvFilter
```

### Test Organization

| Module           | Test File                 | Tests |
| ---------------- | ------------------------- | ----- |
| ConvFilterModel  | ConvFilterModel_uTest.cpp | 9     |
| ConvFilterKernel | ConvFilterKernel_uTest.cu | 13    |

### Expected Output

```
100% tests passed, 0 tests failed out of 22
```

---

## 7. See Also

- `src/sim/gpu_compute/batch_stats/` -- Parallel reduction model
- `src/sim/gpu_compute/fft_analyzer/` -- Batched FFT analysis model
- `src/sim/gpu_compute/stream_compact/` -- Stream compaction model
- `src/sim/gpu_compute/ptst/GpuCompute_pTest.cu` -- Shared performance tests
