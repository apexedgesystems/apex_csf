# Legendre Polynomials Math Library

**Namespace:** `apex::math::legendre`
**Platform:** Cross-platform (CPU); Linux with CUDA toolkit (GPU)
**C++ Standard:** C++17

High-performance library for fully normalized associated Legendre functions (Pbar), designed for real-time and embedded systems with zero-allocation APIs and optional GPU acceleration. Scalar functions evaluate a single P_n^m or Pbar_n^m; the triangle API computes every degree and order up to N at once.

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

**Single value (scalar API):**

```cpp
#include "src/utilities/math/legendre/inc/AssociatedLegendre.hpp"

using apex::math::legendre::normalizedAssociatedLegendre;

// Pbar_{180,90}(0.3): O(n - m), no allocation, RT-safe
double pbar = normalizedAssociatedLegendre(180, 90, 0.3);

// Repeated evaluation up to a fixed degree: build the tables once (allocates)
using apex::math::legendre::makeScalarTables;
const auto TABLES = makeScalarTables(2190);
double fast = normalizedAssociatedLegendre(180, 90, 0.3, TABLES); // RT-safe
```

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

- **Scalar** - One Pbar_n^m by walking its column, O(n - m), ~0.39us for (180, 90)
- **Scalar cached** - The same walk from precomputed tables, ~0.12us for (180, 90)
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

### Scalar API (AssociatedLegendre.hpp)

| Function                                    | RT-Safe | Notes                                                  |
| ------------------------------------------- | ------- | ------------------------------------------------------ |
| `normalizedAssociatedLegendre()`            | YES     | O(n - m), no allocation, `APEX_NONBLOCKING`            |
| `associatedLegendre()`                      | YES     | O(n - m), no allocation, `APEX_NONBLOCKING`            |
| `legendrePolynomial()`                      | YES     | O(n), no allocation, `APEX_NONBLOCKING`                |
| `normalizedAssociatedLegendre(..., tables)` | YES     | O(n - m), reads precomputed tables, `APEX_NONBLOCKING` |
| `makeScalarTables()`                        | NO      | Allocates the tables (about 8 N^2 bytes)               |
| `normalizedAssociatedLegendreFunction()`    | NO      | Takes a `std::function` (may allocate to wrap)         |
| `associatedLegendreFunction()`              | NO      | Takes a `std::function` (may allocate to wrap)         |
| `legendrePolynomialFunction()`              | NO      | Takes a `std::function` (may allocate to wrap)         |

The non-blocking contract is enforced under the RealtimeSanitizer build
(`make rtsan`): an allocation reached from these functions aborts the test.

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

### Scalar Functions

`normalizedAssociatedLegendre(n, m, x)` and `associatedLegendre(n, m, x)` walk
column m of the stable upward recurrence from the diagonal (m, m) to (n, m):
O(n - m) work, no table, no allocation. Both omit the Condon-Shortley phase
(geodesy convention), and the normalized form matches the triangle entry at
`pbarTriangleIndex(n, m)`.

| Property     | Normalized `Pbar_n^m`                                  | Unnormalized `P_n^m`                                                  |
| ------------ | ------------------------------------------------------ | --------------------------------------------------------------------- |
| Accuracy     | 1e-10 relative to `std::sph_legendre` through n = 2190 | 1e-10 relative to `std::assoc_legendre`                               |
| Range        | Bounded by sqrt(2n+1); finite at every degree          | Becomes inf where the value exceeds DBL_MAX (`P_n^n(0)` from n = 151) |
| Invalid n, m | NaN                                                    | NaN                                                                   |

Near |x| = 1 with large m the diagonal seed `(1 - x^2)^(m/2)` falls into
subnormals and then to zero (for example x = 0.999 from m near 240), and the
column loses its digits with it. Every forward-column method shares this,
`std::sph_legendre` included; Holmes-Featherstone scaling is the remedy and is
not applied here.

`normalizedAssociatedLegendre(n, m, x, tables)` is the same walk reading its
coefficients from a `ScalarTables` built once by `makeScalarTables(N)`: each
step is two multiplies and a subtract, the diagonal is a table lookup times
s^m, and each column's coefficients are stored contiguously so a walk reads
one forward stream. Degrees beyond the tables return NaN. The tables take
about 8 N^2 bytes (38 MB at N = 2190) and are read-only after construction,
so one instance can serve any number of threads.

The transform variants (`...Function(n, m, fX, x)`) evaluate at `fX(x)` and
clamp the transformed value, so `fX = cos` with a colatitude in [0, pi] works
across the whole range.

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

### Scalar Functions vs the Standard Library

Release build, x = 0.3, pinned to one core under SCHED_FIFO on an x86-64
laptop, `LegendreScalar_PTEST --cycles 200000`; each cell averages the medians
of two runs:

| (n, m)       | stateless (us) | cached (us) | `std::sph_legendre` (us) |
| ------------ | -------------- | ----------- | ------------------------ |
| (10, 3)      | 0.027          | 0.0077      | 0.071                    |
| (50, 25)     | 0.11           | 0.028       | 0.19                     |
| (180, 90)    | 0.39           | 0.12        | 0.65                     |
| (180, 180)   | 0.17           | 0.015       | 0.033                    |
| (720, 360)   | 1.59           | 0.49        | 2.71                     |
| (2190, 1000) | 5.3            | 1.6         | 8.5                      |

Off the diagonal the stateless walk is 1.6-2.6x faster than the standard
library; on the diagonal (n = m) the standard library's closed form is about
5x faster than it, because the walk forms the diagonal product term by term.
The cached walk is 5-9x faster than the standard library off the diagonal and
about 2x faster on it.

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
docker compose run --rm -T dev-cuda ctest --test-dir build/hosted-x86_64-debug -L legendre

# Run performance tests
docker compose run --rm -T dev-cuda ./build/hosted-x86_64-debug/bin/ptests/LegendrePbar_PTEST
docker compose run --rm -T dev ./build/hosted-x86_64-release/bin/ptests/LegendreScalar_PTEST --cycles 200000
```

### Test Organization

| Test File                           | Purpose                       | Tests |
| ----------------------------------- | ----------------------------- | ----- |
| `LegendrePolynomials_uTest.cpp`     | Scalar functions, to n = 2190 | 19    |
| `LegendrePbarTriangle_uTest.cpp`    | CPU triangle computation      | 5     |
| `LegendrePbarDerivatives_uTest.cpp` | Derivative computation        | 11    |
| `LegendrePbarCuda_uTest.cu`         | GPU/CPU parity                | 13    |
| `LegendreScalar_pTest.cpp`          | Scalar vs `std::` throughput  | 4 x 6 |
| `LegendrePbar_pTest.cpp`            | Triangle benchmarks           | 8     |

---

## 7. See Also

- `src/sim/environment/gravity/` - EGM2008 gravity model using this library
- [Vernier](https://github.com/apexedgesystems/vernier) - Performance testing framework
- `src/utilities/compatibility/` - CUDA attribute shims and C++17/20 compatibility
