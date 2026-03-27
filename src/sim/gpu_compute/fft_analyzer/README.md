# FFTAnalyzer Model

**Namespace:** `sim::gpu_compute`
**Platform:** Linux (CUDA optional)
**C++ Standard:** C++23

GPU batched FFT analyzer using cuFFT R2C transform with custom magnitude/peak kernel for spectral analysis.

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
#include "src/sim/gpu_compute/fft_analyzer/inc/FFTAnalyzerModel.hpp"

sim::gpu_compute::FFTAnalyzerModel model;
model.init();

// Kick launches batched FFT + peak detection
model.kick();

// Poll checks completion (non-blocking)
model.poll();

const auto& STATE = model.state();
fmt::print("Peak: {:.1f} Hz at {:.1f} dB\n",
           STATE.lastPeakFreqHz, STATE.lastPeakMagnitudeDb);
```

---

## 2. Key Features

| Feature               | Detail                                                              |
| --------------------- | ------------------------------------------------------------------- |
| Batched cuFFT         | R2C forward transform over many channels simultaneously             |
| Peak detection        | Per-channel magnitude spectrum (dB) with warp-shuffle max reduction |
| Noise floor estimate  | Average magnitude per channel for SNR assessment                    |
| Async kick/poll       | Non-blocking CPU-side scheduling; GPU runs on its own stream        |
| Configurable channels | Up to thousands of sensor channels at power-of-2 sample counts      |

---

## 3. Common Workflows

### Spectral Analysis

```cpp
sim::gpu_compute::FFTAnalyzerTunableParams params;
params.channelCount = 512;
params.samplesPerChannel = 8192; // Must be power of 2
params.sampleRateHz = 44100.0f;
params.peakThresholdDb = -30.0f;

// Model uses cuFFT for R2C transform, then custom kernel for:
// 1. Magnitude spectrum: |X[k]| in dB
// 2. Peak extraction: strongest bin per channel
// 3. Noise floor: average magnitude per channel
```

---

## 4. API Reference

### FFTAnalyzerModel

**Header:** `FFTAnalyzerModel.hpp`
**Component ID:** 131

| Method       | RT-Safe | Purpose                                                   |
| ------------ | ------- | --------------------------------------------------------- |
| `init()`     | No      | Allocate device memory, create cuFFT plan and CUDA stream |
| `kick()`     | Yes     | Execute cuFFT + magnitude/peak kernel                     |
| `poll()`     | Yes     | Non-blocking completion check                             |
| `teardown()` | No      | Free device memory, destroy cuFFT plan                    |

### CUDA Kernel API

**Header:** `FFTAnalyzerKernel.cuh`
**Namespace:** `sim::gpu_compute::cuda`

| Function                  | RT-Safe | Purpose                                               |
| ------------------------- | ------- | ----------------------------------------------------- |
| `fftMagnitudePeaksCuda()` | Yes     | Magnitude spectrum (dB) + per-channel peak extraction |

### Data Structures

| Struct                     | Purpose                                                    |
| -------------------------- | ---------------------------------------------------------- |
| `FFTAnalyzerTunableParams` | Runtime config (24 bytes, static_assert verified)          |
| `FFTAnalyzerState`         | Execution tracking + last peak frequency/magnitude         |
| `ChannelPeak`              | Per-channel output: peak freq, magnitude, bin, noise floor |

---

## 5. Requirements

| Requirement  | Detail                                        |
| ------------ | --------------------------------------------- |
| Compiler     | C++23 + CUDA (optional)                       |
| Platform     | Linux                                         |
| Dependencies | system_component, compatibility, cuFFT        |
| GPU          | NVIDIA with CUDA runtime and cuFFT (optional) |

---

## 6. Testing

Run tests using the standard Docker workflow:

```bash
# Build
make compose-debug

# Run all tests
make compose-testp

# Run fft_analyzer tests only
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -R FFTAnalyzer
```

### Test Organization

| Module            | Test File                  | Tests |
| ----------------- | -------------------------- | ----- |
| FFTAnalyzerModel  | FFTAnalyzerModel_uTest.cpp | 14    |
| FFTAnalyzerKernel | FFTAnalyzerKernel_uTest.cu | 7     |

### Expected Output

```
100% tests passed, 0 tests failed out of 21
```

---

## 7. See Also

- `src/sim/gpu_compute/batch_stats/` -- Parallel reduction model
- `src/sim/gpu_compute/conv_filter/` -- 2D convolution model
- `src/sim/gpu_compute/stream_compact/` -- Stream compaction model
- `src/sim/gpu_compute/ptst/GpuCompute_pTest.cu` -- Shared performance tests
