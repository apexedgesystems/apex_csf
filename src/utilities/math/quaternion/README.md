# Quaternion Math Library

**Namespace:** `apex::math::quaternion`
**Platform:** Cross-platform (CPU); Linux with CUDA toolkit (GPU)
**C++ Standard:** C++17

Unit quaternion operations for 3D rotations with RT-safe APIs and optional GPU acceleration for batch processing.

---

## Table of Contents

1. [Quick Start](#1-quick-start)
2. [Key Features](#2-key-features)
3. [RT-Safety](#3-rt-safety)
4. [API Reference](#4-api-reference)
5. [Performance](#5-performance)
6. [CUDA Support](#6-cuda-support)
7. [Testing](#7-testing)
8. [See Also](#8-see-also)

---

## 1. Quick Start

`Quaternion<T>` is a non-owning VIEW over caller storage; `QuatData<T>` owns
the four scalars when nothing else does. Every operation returns a `uint8_t`
status.

```cpp
#include "src/utilities/math/quaternion/inc/QuatData.hpp"
#include "src/utilities/math/quaternion/inc/QuaternionIntegrator.hpp"

using namespace apex::math::quaternion;

QuatData<double> qd;                 // owned storage, identity by default
Quaternion<double> q = qd.view();    // math view over it

// Set from angle-axis (axis pre-normalized) or Euler 3-2-1
q.setFromAngleAxis(0.785398, 0.0, 0.0, 1.0);
q.setFromEuler321(roll, pitch, yaw);

// Rotate a vector: v' = q * v * q^{-1}
double vIn[3] = {1.0, 0.0, 0.0}, vOut[3];
q.rotateVectorInto(vIn, vOut);

// Compose, invert, interpolate (caller supplies output storage)
double outD[4];
Quaternion<double> out(outD);
q.multiplyInto(other, out);
q.slerpInto(other, 0.5, out);

// Integrate attitude from body rates (the 6DOF per-tick update)
const double W[3] = {p, qRate, r};
QuaternionIntegrator<double>::stepExponential(q, W, dt);
```

---

## 2. Key Features

### Design Principles

| Principle            | Description                                           |
| -------------------- | ----------------------------------------------------- |
| **Unit quaternions** | All operations assume unit quaternions for rotations  |
| **RT-safe core**     | Core operations are `noexcept` with bounded execution |
| **Status codes**     | No exceptions; operations return `Status` codes       |
| **Layout**           | [w, x, y, z] storage order (scalar-first)             |
| **CUDA batch**       | GPU acceleration for high-throughput workloads        |

### Conventions

- **Quaternion format:** `[w, x, y, z]` where `w` is the scalar part
- **Rotation direction:** Active rotation (rotates vectors, not frames)
- **Handedness:** Right-handed coordinate system
- **Matrix format:** Row-major 3x3 for `toRotationMatrix`
- **Euler angles:** aerospace 3-2-1 (yaw, then pitch, then roll), radians

### Companion headers

| Header                     | Provides                                                                                                                                                                                              |
| -------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `QuatData.hpp`             | `QuatData<T>` -- owned flat-POD storage (identity default, trivially copyable / bus-streamable) with a `view()` accessor                                                                              |
| `QuaternionIntegrator.hpp` | `QuaternionIntegrator<T>` -- attitude steps for `dq/dt = 0.5 q (0, omega)`: `stepEuler`, `stepMidpoint`, `stepExponential` (exact for constant omega), plus the `deltaInto` exponential-map primitive |

### Freestanding / MCU use

The library is `BAREMETAL`-flagged: the same headers compile on the bare-metal
toolchains (arm-none-eabi, avr, pico, esp32, c2000) under
`-fno-exceptions -fno-rtti` at the C++17 floor. Scalar math routes through
`apex::compat` (`compat_math.hpp`): `std::` on hosted builds, `<math.h>` on
freestanding toolchains without a C++ standard library, with float overloads
dispatching to the `f`-suffixed forms so single-precision FPUs never promote
through double. Headers use C-header spellings (`<stdint.h>`, `<stddef.h>`)
for the same reason. `float` instantiations serve single-precision FPUs;
`double` serves the sim.

---

## 3. RT-Safety

| Function                  | RT-Safe | Notes                           |
| ------------------------- | ------- | ------------------------------- |
| `setIdentity()` / `set()` | Yes     | No allocation                   |
| `setFromAngleAxis()`      | Yes     | Axis pre-normalized             |
| `setFromEuler321()`       | Yes     | No allocation                   |
| `rotateVectorInto()`      | Yes     | Assumes unit quaternion         |
| `multiplyInto()`          | Yes     | No allocation                   |
| `conjugateInto()`         | Yes     | No allocation                   |
| `inverseInto()`           | Yes     | Guards zero norm                |
| `normalizeInPlace()`      | Yes     | Guards zero norm                |
| `slerpInto()`             | Yes     | Fast-path approximations        |
| `toRotationMatrixInto()`  | Yes     | Row-major 3x3                   |
| `toAngleAxisInto()`       | Yes     | Angle in [0, pi]                |
| `toEuler321Into()`        | Yes     | Pitch clamps at the singularity |
| `QuaternionIntegrator<T>` | Yes     | All steps O(1), renormalizing   |
| `cuda::*BatchCuda()`      | Yes     | Pre-allocated device buffers    |

---

## 4. API Reference

### Quaternion Class

```cpp
template <typename T>
class Quaternion {              // non-owning view over T[4] = [w, x, y, z]
public:
  explicit Quaternion(T* data) noexcept;

  // Data access (references write through to the viewed storage)
  T& w() noexcept;  T w() const noexcept;   // likewise x(), y(), z()
  T* data() noexcept;
  const T* data() const noexcept;

  // Set operations
  uint8_t setIdentity() noexcept;
  uint8_t set(T w, T x, T y, T z) noexcept;
  uint8_t setFromAngleAxis(T angleRad, T axisX, T axisY, T axisZ) noexcept;
  uint8_t setFromEuler321(T rollRad, T pitchRad, T yawRad) noexcept;

  // Operations (caller supplies output storage)
  uint8_t normInto(T& out) const noexcept;
  uint8_t normalizeInPlace() noexcept;
  uint8_t conjugateInto(Quaternion& out) const noexcept;
  uint8_t inverseInto(Quaternion& out) const noexcept;
  uint8_t multiplyInto(const Quaternion& b, Quaternion& out) const noexcept;
  uint8_t rotateVectorInto(const T* vIn, T* vOut) const noexcept;
  uint8_t slerpInto(const Quaternion& b, T t, Quaternion& out) const noexcept;

  // Conversions
  uint8_t toRotationMatrixInto(T* matOut) const noexcept;
  uint8_t toAngleAxisInto(T& angleRad, T& ax, T& ay, T& az) const noexcept;
  uint8_t toEuler321Into(T& rollRad, T& pitchRad, T& yawRad) const noexcept;
};

template <typename T> struct QuatData {   // owned flat POD, identity default
  T d[4];
  Quaternion<T> view() noexcept;
  T w() const noexcept;                    // likewise x(), y(), z()
};

template <typename T> class QuaternionIntegrator {  // stateless steps
public:
  static uint8_t deltaInto(const T* omegaBody, T dt, Quaternion<T>& out) noexcept;
  static uint8_t stepEuler(Quaternion<T>& q, const T* omegaBody, T dt) noexcept;
  static uint8_t stepMidpoint(Quaternion<T>& q, const T* omegaBody, T dt) noexcept;
  static uint8_t stepExponential(Quaternion<T>& q, const T* omegaBody, T dt) noexcept;
};
```

### Status Codes

All operations return `uint8_t` values of `Status` (`QuaternionStatus.hpp`):

```cpp
enum class Status : uint8_t {
  SUCCESS = 0,
  ERROR_INVALID_VALUE,   // zero norm in normalize/inverse
  ERROR_SIZE_MISMATCH,
  ERROR_SINGULAR,
  ERROR_NOT_NORMALIZED,
  ERROR_UNSUPPORTED_OP,
  ERROR_UNKNOWN
};
```

---

## 5. Performance

CPU throughput on x86-64 (batch of 10,000 quaternions, 15 repeats):

| Operation          | Median (us) | Per-Op (ns) | CV%  |
| ------------------ | ----------- | ----------- | ---- |
| Multiply           | 195.8       | 19.6        | 1.9% |
| Rotate Vector      | 131.2       | 13.1        | 1.4% |
| SLERP              | 600.1       | 60.0        | 2.4% |
| To Rotation Matrix | 119.8       | 12.0        | 1.9% |

SLERP is ~3.1x slower than multiply due to transcendental functions (acos, sin, cos).
The other operations are pure element-wise arithmetic.

Attitude-integration steps and Euler conversions (single call, not batched):

| Operation            | Per-Call (ns) |
| -------------------- | ------------- |
| `stepEuler`          | 72            |
| `stepMidpoint`       | 56            |
| `stepExponential`    | 53            |
| Euler-321 round trip | 75            |

All are noise against a control tick: at 50 Hz the exponential step spends
~0.0003% of the period.

GPU batch throughput (10,000 quaternions, CUDA):

| Operation     | GPU Kernel (us) | CPU vs GPU Speedup |
| ------------- | --------------- | ------------------ |
| Rotate Vector | 6.2             | ~96x               |
| SLERP         | 21.9            | ~211x              |
| Multiply      | 6.4             | ~31x               |

---

## 6. CUDA Support

CUDA batch operations for high-throughput quaternion processing.

### Usage

```cpp
#include "src/utilities/math/quaternion/inc/QuaternionCuda.cuh"
using namespace apex::math::quaternion::cuda;

// Batch vector rotation on GPU
// Quaternions: [w,x,y,z] * batch, Vectors: [x,y,z] * batch
bool ok = rotateVectorBatchCuda(dQs, dVsIn, batch, dVsOut, stream);

// Batch SLERP interpolation
ok = slerpBatchCuda(dQsA, dQsB, dTs, batch, dQsOut, stream);

// Batch quaternion multiply
ok = multiplyBatchCuda(dQsA, dQsB, batch, dQsOut, stream);
```

### CUDA Functions

| Function                    | Description                   | Elements/item       |
| --------------------------- | ----------------------------- | ------------------- |
| `rotateVectorBatchCuda`     | Rotate vectors by quaternions | 4 + 3 in, 3 out     |
| `slerpBatchCuda`            | SLERP interpolation           | 4 + 4 + 1 in, 4 out |
| `normalizeBatchCuda`        | Normalize quaternions         | 4 in/out            |
| `multiplyBatchCuda`         | Hamilton product              | 4 + 4 in, 4 out     |
| `toRotationMatrixBatchCuda` | Convert to rotation matrices  | 4 in, 9 out         |

All functions have `*F` variants for `float` precision.

### Linking CUDA Library

```cmake
target_link_libraries(your_target PRIVATE utilities_math_quaternion utilities_math_quaternion_cuda)
```

---

## 7. Testing

Run tests using the standard Docker workflow:

```bash
# Build
docker compose run --rm -T dev-cuda make debug

# Run quaternion tests
docker compose run --rm -T dev-cuda ctest --test-dir build/hosted-x86_64-debug -L quaternion
```

### Test Organization

| Test File                 | Coverage                  | Tests |
| ------------------------- | ------------------------- | ----- |
| `Quaternion_uTest.cpp`    | CPU quaternion operations | 22    |
| `QuaternionCuda_uTest.cu` | CUDA batch operations     | 2     |

### Expected Output

```
100% tests passed, 0 tests failed out of 24
```

---

## 8. See Also

- `src/utilities/math/linalg/` - Linear algebra (Matrix3 for DCM)
- `src/utilities/math/integration/` - ODE integration (uses Quaternion for state)
- `src/utilities/math/legendre/` - Legendre polynomials (CPU + CUDA pattern)
