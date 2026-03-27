# Legendre Polynomials Math Library

**Namespace:** `apex::math::legendre`
**Platform:** Cross-platform (CPU); Linux with CUDA toolkit (GPU)
**C++ Standard:** C++17

High-performance library for fully normalized associated Legendre functions (Pbar), designed for real-time and embedded systems with zero-allocation APIs and optional GPU acceleration.

---

## Table of Contents

1. [Quick Start](#1-quick-start)
2. [Key Features](#2-key-features)
3. [RT-Safety](#3-rt-safety)
4. [API Reference](#4-api-reference)
5. [Performance](#5-performance)
6. [Testing](#6-testing)
7. [See Also](#7-see-also)

---

## 1. Quick Start

**Simple triangle computation:**

```cpp
#include "src/utilities/math/legendre/inc/PbarTriangle.hpp"

using apex::math::legendre::computeNormalizedPbarTriangleVector;
using apex::math::legendre::pbarTriangleIndex;

// Compute fully normalized triangle for N=50
auto triangle = computeNormalizedPbarTriangleVector(50, 0.5);

// Access Pbar_{n,m} via triangular index
double pbar_10_5 = triangle[pbarTriangleIndex(10, 5)];
```

**High-performance path (precomputed coefficients):**

```cpp
#include "src/utilities/math/legendre/inc/PbarTriangle.hpp"

using apex::math::legendre::computeRecurrenceCoefficientsVector;
using apex::math::legendre::computeNormalizedPbarTriangleCached;
using apex::math::legendre::pbarTriangleSize;

constexpr int N = 180;
const auto SIZE = pbarTriangleSize(N);

// One-time precomputation (NOT RT-safe)
const auto COEFFS = computeRecurrenceCoefficientsVector(N);
std::vector<double> out(SIZE);

// Hot path: 3-4x faster than non-cached (RT-safe)
for (double x : samples) {
  computeNormalizedPbarTriangleCached(N, x, COEFFS.A.data(), COEFFS.B.data(),
                                       out.data(), SIZE);
  // Use out...
}
```

**With derivatives (for gravity field acceleration):**

```cpp
#include "src/utilities/math/legendre/inc/PbarDerivatives.hpp"

using apex::math::legendre::computeNormalizedPbarTriangleWithDerivativesVector;
using apex::math::legendre::pbarTriangleIndex;

double sinPhi = 0.5;
double cosPhi = std::sqrt(1.0 - sinPhi * sinPhi);
auto result = computeNormalizedPbarTriangleWithDerivativesVector(50, sinPhi, cosPhi);

// result.P[idx] = Pbar_{n,m}(sin(phi))
// result.dP[idx] = dPbar_{n,m}/dphi
double pbar = result.P[pbarTriangleIndex(10, 5)];
double dpbar = result.dP[pbarTriangleIndex(10, 5)];
```

---

## 2. Key Features

### Computation Modes

- **CPU baseline** - Standard upward recurrence, ~77us for N=180
- **CPU cached** - Precomputed A/B coefficients, ~33us for N=180 (2.3x faster)
- **GPU kernel** - CUDA parallel computation, ~4us for N=180 (19x faster)
- **GPU batch** - 32 samples at once, ~0.2us per triangle (385x faster)

### Design Goals

- **Zero-allocation hot path** - RT-safe APIs use caller-provided buffers
- **Predictable triangular storage** - `idx(n,m) = n(n+1)/2 + m`
- **EGM2008/IERS compatible** - Fully normalized (barred) convention
- **GPU transparency** - CUDA headers isolated, streams passed as `void*`

---

## 3. RT-Safety

### Triangle API (PbarTriangle.hpp)

| Function                                | RT-Safe | Notes                                   |
| --------------------------------------- | ------- | --------------------------------------- |
| `pbarTriangleSize()`                    | YES     | Inline, no allocations                  |
| `pbarTriangleIndex()`                   | YES     | Inline, no allocations                  |
| `computeNormalizedPbarTriangle()`       | YES     | Zero-allocation, uses caller buffer     |
| `computeNormalizedPbarTriangleCached()` | YES     | Zero-allocation, uses precomputed A/B   |
| `computeNormalizedPbarTriangleVector()` | NO      | Returns std::vector (allocates)         |
| `computeRecurrenceCoefficients()`       | YES     | Zero-allocation, uses caller buffer     |
| `computeRecurrenceCoefficientsVector()` | NO      | Returns struct with vectors (allocates) |

### Derivative API (PbarDerivatives.hpp)

| Function                                               | RT-Safe | Notes                                   |
| ------------------------------------------------------ | ------- | --------------------------------------- |
| `betaCoefficient()`                                    | YES     | Inline, no allocations                  |
| `computeBetaCoefficients()`                            | YES     | Zero-allocation, uses caller buffer     |
| `computeBetaCoefficientsVector()`                      | NO      | Returns std::vector (allocates)         |
| `computeNormalizedPbarTriangleWithDerivatives()`       | YES     | Zero-allocation, uses caller buffers    |
| `computeNormalizedPbarTriangleWithDerivativesCached()` | YES     | Zero-allocation, uses precomputed beta  |
| `computeNormalizedPbarTriangleWithDerivativesVector()` | NO      | Returns struct with vectors (allocates) |

### GPU API (PbarTriangleCuda.cuh)

| Function                                                     | RT-Safe | Notes                             |
| ------------------------------------------------------------ | ------- | --------------------------------- |
| `computeNormalizedPbarTriangleCuda()`                        | NO      | Allocates temp device buffer      |
| `computeNormalizedPbarTriangleCudaPrealloc()`                | YES     | Uses pre-allocated device buffers |
| `computeNormalizedPbarTriangleBatchCuda()`                   | YES     | Batched, optional precomputed A/B |
| `computeNormalizedPbarTriangleWithDerivativesCudaPrealloc()` | YES     | P + dP with pre-allocated inputs  |
| `computeNormalizedPbarTriangleWithDerivativesBatchCuda()`    | YES     | Batched P + dP                    |

### Workspace API (PbarWorkspace.hpp)

| Function                   | RT-Safe | Notes                                 |
| -------------------------- | ------- | ------------------------------------- |
| `createPbarWorkspace()`    | NO      | Allocates GPU and pinned host buffers |
| `ensurePbarCoefficients()` | NO      | One-time coefficient build            |
| `enqueueCompute()`         | YES     | Enqueues kernel, no allocation        |
| `synchronize()`            | YES     | Waits for completion                  |
| `destroyPbarWorkspace()`   | NO      | Frees resources                       |

---

## 4. API Reference

### Triangular Storage

All functions use triangular storage with index formula:

```
idx(n,m) = n(n+1)/2 + m
size(N) = (N+1)(N+2)/2
```

For N=180, this is 16,471 coefficients.

### CPU: Precomputed Coefficients (Recommended)

For repeated evaluations, precompute recurrence coefficients once:

```cpp
// One-time setup (NOT RT-safe)
const auto COEFFS = computeRecurrenceCoefficientsVector(N);
std::vector<double> out(pbarTriangleSize(N));

// Hot path: 3-4x faster (RT-safe)
computeNormalizedPbarTriangleCached(N, x, COEFFS.A.data(), COEFFS.B.data(),
                                     out.data(), out.size());
```

### GPU: Workspace API

For high-throughput GPU computation:

```cpp
#include "src/utilities/math/legendre/inc/PbarWorkspace.hpp"

using apex::math::legendre::PbarWorkspace;
using apex::math::legendre::createPbarWorkspace;
using apex::math::legendre::ensurePbarCoefficients;
using apex::math::legendre::enqueueCompute;
using apex::math::legendre::synchronize;
using apex::math::legendre::destroyPbarWorkspace;

PbarWorkspace ws{};
createPbarWorkspace(ws, /*n=*/180, /*batch=*/32, /*pinnedHost=*/true);
ensurePbarCoefficients(ws);  // one-time A/B build on device

// Fill ws.hXs with input values...
enqueueCompute(ws);
synchronize(ws);
// Results in ws.hOut

destroyPbarWorkspace(ws);
```

### GPU: With Derivatives

```cpp
#include "src/utilities/math/legendre/inc/PbarWorkspace.hpp"

using apex::math::legendre::createPbarWorkspaceWithDerivatives;
using apex::math::legendre::ensureBetaCoefficients;
using apex::math::legendre::enqueueComputeWithDerivatives;

PbarWorkspace ws{};
createPbarWorkspaceWithDerivatives(ws, 180, 32, true);
ensurePbarCoefficients(ws);
ensureBetaCoefficients(ws);

// Fill ws.hXs (sin phi) and ws.hCosPhis (cos phi)...
enqueueComputeWithDerivatives(ws);
synchronize(ws);
// P in ws.hOut, dP/dphi in ws.hDpOut

destroyPbarWorkspace(ws);
```

---

## 5. Performance

### Benchmark Results (15 repeats)

| Implementation        | N   | Median (us)     | CV%  | Throughput |
| --------------------- | --- | --------------- | ---- | ---------- |
| CPU baseline          | 50  | 36.3            | 2.5% | 27.6K/s    |
| CPU baseline          | 100 | 151.1           | 2.5% | 6.6K/s     |
| CPU baseline          | 180 | 450.6           | 0.7% | 2.2K/s     |
| CPU baseline          | 360 | 1860.0          | 1.9% | 538/s      |
| CPU cached            | 180 | 47.7            | 2.1% | 20.9K/s    |
| GPU kernel (batch 32) | 180 | 2442.6 (kernel) | -    | -          |

CPU cached is 9.4x faster than baseline at N=180 (47.7 vs 450.6 us).

### Scaling with Degree N

| N   | CPU Baseline (us) | Ratio |
| --- | ----------------- | ----- |
| 50  | 36.3              | 0.08x |
| 100 | 151.1             | 0.34x |
| 180 | 450.6             | 1.0x  |
| 360 | 1860.0            | 4.1x  |

Scaling is O(N^2) as expected (triangular storage of (N+1)(N+2)/2 elements).

### Why Cached is Faster

The upward recurrence requires coefficients A(n,m) and B(n,m) containing sqrt() calls:

```
A(n,m) = sqrt(((2n+1)(2n-1)) / ((n-m)(n+m)))
B(n,m) = sqrt(((2n+1)(n+m-1)(n-m-1)) / ((2n-3)(n-m)(n+m)))
```

For N=180, this is ~32,400 sqrt() pairs per triangle. Precomputing these once and reusing across calls eliminates this bottleneck.

---

## 6. Testing

Run tests using the standard Docker workflow:

```bash
# Build
docker compose run --rm -T dev-cuda make debug

# Run unit tests
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -L legendre

# Run performance tests
docker compose run --rm -T dev-cuda ./build/native-linux-debug/bin/ptests/MathLegendre_PTEST
```

### Test Organization

| Test File                           | Purpose                       | Tests |
| ----------------------------------- | ----------------------------- | ----- |
| `LegendreFactorials_uTest.cpp`      | Factorial precomputation      | 4     |
| `LegendrePolynomials_uTest.cpp`     | Associated Legendre functions | 8     |
| `LegendrePbarTriangle_uTest.cpp`    | CPU triangle computation      | 12    |
| `LegendrePbarDerivatives_uTest.cpp` | Derivative computation        | 6     |
| `LegendrePbarCuda_uTest.cu`         | GPU/CPU parity                | 12    |
| `LegendrePbar_pTest.cpp`            | Performance benchmarks        | 10    |

---

## 7. See Also

- `src/sim/kinematics/gravity/` - EGM2008 gravity model using this library
- [Vernier](https://github.com/apexedgesystems/vernier) - Performance testing framework
- `src/utilities/compatibility/` - CUDA attribute shims and C++17/20 compatibility
- `optimization/legendre/` - Optimization notes and baseline measurements
