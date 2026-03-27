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

**Basic quaternion operations:**

```cpp
#include "src/utilities/math/quaternion/inc/Quaternion.hpp"

using apex::math::quaternion::Quaternion;

// Identity quaternion (w=1, x=y=z=0)
Quaternion<double> q = Quaternion<double>::identity();

// Create from axis-angle
double axis[3] = {0.0, 0.0, 1.0};
auto qRot = Quaternion<double>::fromAxisAngle(axis, M_PI / 4.0);

// Rotate a vector
double vIn[3] = {1.0, 0.0, 0.0};
double vOut[3];
qRot.rotateVector(vIn, vOut);

// Compose rotations
Quaternion<double> qCompose;
qRot.multiply(q, qCompose);

// SLERP interpolation
Quaternion<double> qInterp;
q.slerp(qRot, 0.5, qInterp);
```

**Convert to/from rotation matrix:**

```cpp
// Quaternion to rotation matrix (row-major 3x3)
double mat[9];
q.toRotationMatrix(mat);

// Matrix to quaternion
auto qFromMat = Quaternion<double>::fromRotationMatrix(mat);
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

---

## 3. RT-Safety

| Function               | RT-Safe | Notes                        |
| ---------------------- | ------- | ---------------------------- |
| `identity()`           | Yes     | Returns static value         |
| `fromAxisAngle()`      | Yes     | No allocation                |
| `rotateVector()`       | Yes     | No allocation                |
| `multiply()`           | Yes     | No allocation                |
| `conjugate()`          | Yes     | No allocation                |
| `inverse()`            | Yes     | No allocation (assumes unit) |
| `slerp()`              | Yes     | No allocation                |
| `toRotationMatrix()`   | Yes     | No allocation                |
| `fromRotationMatrix()` | Yes     | No allocation                |
| `cuda::*BatchCuda()`   | Yes     | Pre-allocated device buffers |

---

## 4. API Reference

### Quaternion Class

```cpp
template <typename T>
class Quaternion {
public:
  // Data access
  T w() const noexcept;
  T x() const noexcept;
  T y() const noexcept;
  T z() const noexcept;
  T* data() noexcept;
  const T* data() const noexcept;

  // Static constructors
  static Quaternion identity() noexcept;
  static Quaternion fromAxisAngle(const T* axis, T angle) noexcept;
  static Quaternion fromRotationMatrix(const T* mat) noexcept;
  static Quaternion fromEulerZYX(T z, T y, T x) noexcept;

  // Operations
  Status rotateVector(const T* vIn, T* vOut) const noexcept;
  Status multiply(const Quaternion& rhs, Quaternion& out) const noexcept;
  Status conjugate(Quaternion& out) const noexcept;
  Status inverse(Quaternion& out) const noexcept;
  Status normalize() noexcept;
  Status slerp(const Quaternion& target, T t, Quaternion& out) const noexcept;

  // Conversions
  Status toRotationMatrix(T* mat) const noexcept;
  Status toAxisAngle(T* axis, T& angle) const noexcept;
  Status toEulerZYX(T& z, T& y, T& x) const noexcept;
};
```

### Status Codes

```cpp
enum class Status : std::uint8_t {
  SUCCESS = 0,
  ERROR_ZERO_NORM = 1,
  ERROR_INVALID_AXIS = 2,
  ERROR_NOT_UNIT = 3,
  ERROR_INVALID_MATRIX = 4,
  ERROR_GIMBAL_LOCK = 5,
  ERROR_CUDA_UNAVAILABLE = 6,
  ERROR_CUDA_KERNEL = 7,
  ERROR_INVALID_BATCH = 8,
  ERROR_NULL_POINTER = 9
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
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -L quaternion
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
