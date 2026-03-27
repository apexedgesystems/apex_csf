# Linear Algebra Library

**Namespace:** `apex::math::linalg`
**Platform:** Cross-platform (Linux, CUDA)
**C++ Standard:** C++17

Non-owning 2D array views with optional BLAS/LAPACK acceleration for RT-safe linear algebra operations. Includes CUDA batch processing for high-throughput workloads.

---

## Table of Contents

1. [Quick Start](#1-quick-start)
2. [Key Features](#2-key-features)
3. [API Reference](#3-api-reference)
4. [Performance](#4-performance)
5. [CUDA Support](#5-cuda-support)
6. [Requirements](#6-requirements)
7. [Testing](#7-testing)
8. [See Also](#8-see-also)

---

## 1. Quick Start

### Headers

```cpp
#include "src/utilities/math/linalg/inc/ArrayBase.hpp"    // Non-owning view
#include "src/utilities/math/linalg/inc/Array.hpp"        // BLAS-accelerated ops
#include "src/utilities/math/linalg/inc/ArrayOps.hpp"     // Elementwise ops, norms
#include "src/utilities/math/linalg/inc/Vector.hpp"       // Vector operations
#include "src/utilities/math/linalg/inc/Matrix2.hpp"      // 2x2 matrix (RT-safe)
#include "src/utilities/math/linalg/inc/Matrix3.hpp"      // 3x3 matrix (DCM)
#include "src/utilities/math/linalg/inc/Matrix4.hpp"      // 4x4 matrix (homogeneous)
#include "src/utilities/math/linalg/inc/Rotations.hpp"    // Rotation utilities
#include "src/utilities/math/linalg/inc/ArrayStatus.hpp"  // Status codes
#include "src/utilities/math/linalg/inc/ArrayCuda.cuh"    // CUDA batch ops
```

### Basic Usage

```cpp
using namespace apex::math::linalg;

// Create a non-owning view over existing storage
std::array<double, 6> data = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
Array<double> mat(data.data(), 2, 3, Layout::RowMajor);

// Access elements
double val = mat(0, 1);  // = 2.0

// Bounds-checked access
double out = 0.0;
if (mat.get(1, 2, out) == static_cast<uint8_t>(Status::SUCCESS)) {
  // out = 6.0
}
```

### Matrix Multiplication (GEMM)

```cpp
std::vector<double> aBuf(2 * 3), bBuf(3 * 2), cBuf(2 * 2);
Array<double> a(aBuf.data(), 2, 3, Layout::RowMajor);
Array<double> b(bBuf.data(), 3, 2, Layout::RowMajor);
Array<double> c(cBuf.data(), 2, 2, Layout::RowMajor);

// C = alpha * A * B + beta * C
auto st = a.gemmInto(b, c, 1.0, 0.0);
if (st == static_cast<uint8_t>(Status::SUCCESS)) {
  // c contains A * B
}
```

---

## 2. Key Features

### Design Principles

| Principle            | Description                                            |
| -------------------- | ------------------------------------------------------ |
| **Non-owning views** | No allocations; views reference external storage       |
| **RT-safe core**     | Core operations are `noexcept` with bounded execution  |
| **Status codes**     | No exceptions; all operations return `uint8_t` status  |
| **Layout-aware**     | Explicit row-major/column-major with leading dimension |
| **BLAS/LAPACK**      | Optional acceleration when libraries are available     |

### RT-Safety Table

| Class/Function          | RT-Safe | Notes                           |
| ----------------------- | ------- | ------------------------------- |
| `ArrayBase`             | Yes     | All operations                  |
| `Array::gemmInto`       | Yes     | No allocation                   |
| `Array::transposeInto`  | Yes     | No allocation                   |
| `Array::trace`          | Yes     | No allocation                   |
| `Array::inverseInPlace` | **No**  | Allocates pivot array           |
| `Array::determinant`    | **No**  | Allocates temp buffer           |
| `Vector` (all)          | Yes     | No allocation                   |
| `Matrix2` (all)         | Yes     | Cramer's rule inverse, no alloc |
| `Matrix3` (all)         | Yes     | Cramer's rule inverse, no alloc |
| `Matrix4` (all)         | Yes     | Adjugate inverse, no alloc      |
| `Rotations` (all)       | Yes     | Header-only, no allocation      |
| `skew3Into`             | Yes     | No allocation                   |
| `outerInto`             | Yes     | No allocation                   |
| `frobeniusNormInto`     | Yes     | No allocation                   |
| `infNormInto`           | Yes     | No allocation                   |
| `oneNormInto`           | Yes     | No allocation                   |
| `setIdentity`           | Yes     | No allocation                   |
| `addInto`               | Yes     | No allocation                   |
| `cuda::*`               | Yes     | Pre-allocated device buffers    |

### Class Hierarchy

```
ArrayBase<T, Derived>       Base non-owning 2D view (CRTP)
    |
    +-- Array<T>            BLAS/LAPACK-accelerated operations
            |
            +-- Vector<T>       Vector (Nx1 or 1xN) specialization
            |
            +-- Matrix2<T>      2x2 matrix (RT-safe inverse via Cramer's rule)
            |
            +-- Matrix3<T>      3x3 matrix (DCM, RT-safe inverse via Cramer's rule)
            |
            +-- Matrix4<T>      4x4 matrix (homogeneous, RT-safe inverse via adjugate)

Rotations::                 Header-only rotation utilities
    +-- dcmFromEuler321Into       Euler 3-2-1 to DCM
    +-- eulerFromDcm321Into       DCM to Euler 3-2-1
    +-- dcmFromAxisAngleInto      Rodrigues formula
    +-- axisAngleFromDcmInto      DCM to axis-angle
    +-- dcmFromSmallAnglesInto    First-order approximation

cuda::                      GPU batch operations (ArrayCuda.cuh)
    +-- gemm3x3BatchCuda
    +-- transpose3x3BatchCuda
    +-- inverse3x3BatchCuda
    +-- determinant3x3BatchCuda
    +-- matvec3x3BatchCuda
    +-- cross3BatchCuda / dot3BatchCuda / normalize3BatchCuda
```

---

## 3. API Reference

### ArrayBase

Non-owning 2D array view with element access and subview operations.

```cpp
template <typename T, class Derived = void>
class ArrayBase {
  // Construction
  constexpr ArrayBase(T* data, std::size_t rows, std::size_t cols,
                      Layout layout = Layout::RowMajor,
                      std::size_t ld = 0) noexcept;

  // Accessors
  constexpr std::size_t rows() const noexcept;
  constexpr std::size_t cols() const noexcept;
  constexpr std::size_t size() const noexcept;
  constexpr std::size_t ld() const noexcept;
  constexpr Layout layout() const noexcept;
  constexpr bool isContiguous() const noexcept;

  // Element access (preconditioned - no bounds check)
  T& operator()(std::size_t r, std::size_t c) noexcept;

  // Bounds-checked access
  uint8_t get(std::size_t r, std::size_t c, T& out) const noexcept;
  uint8_t set(std::size_t r, std::size_t c, T val) noexcept;

  // Views
  ArrayBase<T, void> transposeView() const noexcept;
};
```

### Array

BLAS/LAPACK-accelerated operations for `float` and `double`.

```cpp
template <typename T>
class Array final : public ArrayBase<T, Array<T>> {
  // GEMM: C = alpha * A * B + beta * C
  uint8_t gemmInto(const Array& B, Array& C,
                   T alpha = T(1), T beta = T(0)) const noexcept;

  // Transpose: dst = transpose(this)
  uint8_t transposeInto(Array& dst) const noexcept;

  // Inverse (NOT RT-safe)
  uint8_t inverseInPlace() noexcept;
  uint8_t inverseInto(Array& dst) const noexcept;

  // Determinant (NOT RT-safe)
  uint8_t determinant(T& out) const noexcept;

  // Trace (RT-safe)
  uint8_t trace(T& out) const noexcept;
};
```

### Vector

Vector operations with orientation (column or row). All operations RT-safe.

```cpp
template <typename T>
class Vector {
  // Construction
  Vector(Array<T>& view, VectorOrient orient);

  // Properties
  std::size_t size() const noexcept;
  VectorOrient orient() const noexcept;

  // Core operations
  uint8_t dotInto(const Vector& y, T& out) const noexcept;
  uint8_t crossInto(const Vector& y, Vector& out) const noexcept;
  uint8_t norm2Into(T& out) const noexcept;
  uint8_t normalizeInPlace() noexcept;
  uint8_t addInto(const Vector& y, Vector& out) const noexcept;
  uint8_t subInto(const Vector& y, Vector& out) const noexcept;
  uint8_t scaleInto(T alpha, Vector& out) const noexcept;
  uint8_t angleBetweenInto(const Vector& y, T& out) const noexcept;
  uint8_t projectOntoInto(const Vector& y, Vector& out) const noexcept;

  // Elementwise operations
  uint8_t absInto(Vector& out) const noexcept;       // |x_i|
  uint8_t minInto(const Vector& y, Vector& out) const noexcept;  // min(x_i, y_i)
  uint8_t maxInto(const Vector& y, Vector& out) const noexcept;  // max(x_i, y_i)

  // Reductions
  uint8_t sumInto(T& out) const noexcept;   // sum of elements
  uint8_t meanInto(T& out) const noexcept;  // arithmetic mean
};
```

### Matrix2

2x2 fixed-size matrix with RT-safe inverse via Cramer's rule.

```cpp
template <typename T>
class Matrix2 {
  // Construction
  explicit Matrix2(const Array<T>& view) noexcept;
  Matrix2(T* data, Layout layout, std::size_t ld = 0) noexcept;

  // Properties
  static constexpr std::size_t K_ROWS = 2;
  static constexpr std::size_t K_COLS = 2;

  // Matrix operations (RT-safe)
  uint8_t traceInto(T& out) const noexcept;
  Matrix2<T> transposeView() const noexcept;
  uint8_t gemmInto(const Matrix2& b, Matrix2& c,
                   T alpha = T(1), T beta = T(0)) const noexcept;
  uint8_t determinantInto(T& out) const noexcept;  // ad - bc
  uint8_t inverseInPlace() noexcept;               // Cramer's rule
  uint8_t solveInto(const Vector<T>& b, Vector<T>& x) const noexcept;

  // Elementwise arithmetic
  uint8_t addInto(const Matrix2& b, Matrix2& out) const noexcept;
  uint8_t subInto(const Matrix2& b, Matrix2& out) const noexcept;
  uint8_t scaleInto(T alpha, Matrix2& out) const noexcept;
  uint8_t multiplyVecInto(const Vector<T>& x, Vector<T>& y) const noexcept;
};
```

### Matrix3

3x3 matrix convenience wrapper for DCM operations. RT-safe inverse via Cramer's rule.

```cpp
template <typename T>
class Matrix3 {
  // Construction
  explicit Matrix3(Array<T>& view);

  // Properties
  static constexpr std::size_t K_ROWS = 3;
  static constexpr std::size_t K_COLS = 3;

  // Operations (RT-safe)
  uint8_t gemmInto(const Matrix3& b, Matrix3& c,
                   T alpha = T(1), T beta = T(0)) const noexcept;
  uint8_t multiplyVecInto(const Vector<T>& x,
                          Vector<T>& y) const noexcept;
  uint8_t traceInto(T& out) const noexcept;
  uint8_t determinantInto(T& out) const noexcept;  // Sarrus' rule
  uint8_t inverseInPlace() noexcept;               // Cramer's rule
  uint8_t solveInto(const Vector<T>& b, Vector<T>& x) const noexcept;
  Matrix3 transposeView() const noexcept;
};
```

### Matrix4

4x4 fixed-size matrix for homogeneous transforms. RT-safe inverse via adjugate method.

```cpp
template <typename T>
class Matrix4 {
  // Construction
  explicit Matrix4(const Array<T>& view) noexcept;
  Matrix4(T* data, Layout layout, std::size_t ld = 0) noexcept;

  // Properties
  static constexpr std::size_t K_ROWS = 4;
  static constexpr std::size_t K_COLS = 4;

  // Matrix operations (RT-safe)
  uint8_t traceInto(T& out) const noexcept;
  Matrix4<T> transposeView() const noexcept;
  uint8_t gemmInto(const Matrix4& b, Matrix4& c,
                   T alpha = T(1), T beta = T(0)) const noexcept;
  uint8_t determinantInto(T& out) const noexcept;  // Cofactor expansion
  uint8_t inverseInPlace() noexcept;               // Adjugate method
  uint8_t solveInto(const Vector<T>& b, Vector<T>& x) const noexcept;

  // Elementwise arithmetic
  uint8_t addInto(const Matrix4& b, Matrix4& out) const noexcept;
  uint8_t subInto(const Matrix4& b, Matrix4& out) const noexcept;
  uint8_t scaleInto(T alpha, Matrix4& out) const noexcept;
  uint8_t hadamardInto(const Matrix4& b, Matrix4& out) const noexcept;
  uint8_t multiplyVecInto(const Vector<T>& x, Vector<T>& y) const noexcept;
};
```

### ArrayOps Extensions

Additional operations in `ArrayOps.hpp`:

```cpp
// Skew-symmetric matrix from 3-vector
template <typename T>
uint8_t skew3Into(const Vector<T>& v, Matrix3<T>& out) noexcept;

// Outer product: C = a * b^T
template <typename T, class D>
uint8_t outerInto(const Vector<T>& a, const Vector<T>& b, ArrayBase<T, D>& c) noexcept;

// Matrix norms (RT-safe)
template <typename T, class D>
uint8_t frobeniusNormInto(const ArrayBase<T, D>& a, T& out) noexcept;  // sqrt(sum(a_ij^2))

template <typename T, class D>
uint8_t infNormInto(const ArrayBase<T, D>& a, T& out) noexcept;        // max row sum

template <typename T, class D>
uint8_t oneNormInto(const ArrayBase<T, D>& a, T& out) noexcept;        // max col sum
```

### Rotations

Header-only rotation utilities for aerospace applications (3-2-1 Euler convention).

```cpp
// Euler 3-2-1 (yaw-pitch-roll) to DCM
template <typename T>
uint8_t dcmFromEuler321Into(T roll, T pitch, T yaw, Matrix3<T>& dcm) noexcept;

// DCM to Euler 3-2-1 (handles gimbal lock)
template <typename T>
uint8_t eulerFromDcm321Into(const Matrix3<T>& dcm, T& roll, T& pitch, T& yaw) noexcept;

// Axis-angle to DCM (Rodrigues formula)
template <typename T>
uint8_t dcmFromAxisAngleInto(const Vector<T>& axis, T angle, Matrix3<T>& dcm) noexcept;

// DCM to axis-angle
template <typename T>
uint8_t axisAngleFromDcmInto(const Matrix3<T>& dcm, Vector<T>& axis, T& angle) noexcept;

// Small angle approximation (first-order, for perturbation analysis)
template <typename T>
uint8_t dcmFromSmallAnglesInto(T roll, T pitch, T yaw, Matrix3<T>& dcm) noexcept;
```

### Status Codes

```cpp
enum class Status : std::uint8_t {
  SUCCESS = 0,
  ERROR_SIZE_MISMATCH = 1,
  ERROR_OUT_OF_BOUNDS = 2,
  ERROR_INVALID_LAYOUT = 3,
  ERROR_NON_CONTIGUOUS = 4,
  ERROR_NOT_SQUARE = 5,
  ERROR_SINGULAR = 6,
  ERROR_LIB_FAILURE = 7,
  ERROR_UNKNOWN = 8,
  ERROR_UNSUPPORTED = 9,
  ERROR_UNSUPPORTED_OP = 10,
  ERROR_INVALID_VALUE = 11
};
```

---

## 4. Performance

Throughput on x86-64 (15 repeats):

### 3x3 Matrix Operations

| Operation         | Batch | Median (us) | Per-Matrix (ns) | CV%  |
| ----------------- | ----- | ----------- | --------------- | ---- |
| GEMM (C = A \* B) | 1K    | 61.5        | 61.5            | 3.1% |
| Inverse           | 10K   | 331.4       | 33.1            | 1.4% |

### GPU Batch 3x3 (batch of 10,000)

| Kernel    | Median (us) | CV%  |
| --------- | ----------- | ---- |
| GEMM      | 11.3        | 6.7% |
| Inverse   | 12.6        | 2.1% |
| Transpose | 6.9         | 4.6% |

CPU vs GPU speedup: 18,000-35,000x for batch 3x3 operations.
Larger matrices (16x16+) delegate to BLAS (OpenBLAS/cuBLAS).

---

## 5. CUDA Support

CUDA batch operations for high-throughput 3x3 matrix and 3-vector processing.

### Usage

```cpp
#include "src/utilities/math/linalg/inc/ArrayCuda.cuh"
using namespace apex::math::linalg::cuda;

// Batch 3x3 matrix multiply on GPU
// Data layout: row-major, contiguous batch * 9 elements per matrix
bool ok = gemm3x3BatchCuda(dAs, dBs, batch, dCs, stream);

// Batch matrix-vector multiply
ok = matvec3x3BatchCuda(dMats, dVsIn, batch, dVsOut, stream);

// Batch cross product
ok = cross3BatchCuda(dAs, dBs, batch, dCross, stream);
```

### CUDA Functions

| Function                  | Description                      | Elements/item   |
| ------------------------- | -------------------------------- | --------------- | ------------- | --- |
| `gemm3x3BatchCuda`        | C = A \* B for 3x3 matrices      | 9               |
| `transpose3x3BatchCuda`   | B = A^T for 3x3 matrices         | 9               |
| `inverse3x3BatchCuda`     | B = A^{-1} for 3x3 matrices      | 9               |
| `determinant3x3BatchCuda` | det(A) for 3x3 matrices          | 9 in, 1 out     |
| `matvec3x3BatchCuda`      | y = A \* x for 3x3 matrix, 3-vec | 9 + 3 in, 3 out |
| `cross3BatchCuda`         | c = a x b for 3-vectors          | 3               |
| `dot3BatchCuda`           | d = a . b for 3-vectors          | 3 in, 1 out     |
| `normalize3BatchCuda`     | v = v /                          | v               | for 3-vectors | 3   |

All functions have `*F` variants for `float` precision.

### Linking CUDA Library

```cmake
target_link_libraries(your_target PRIVATE utilities_math_linalg utilities_math_linalg_cuda)
```

---

## 6. Requirements

### Build Requirements

- C++17 compiler (GCC 9+, Clang 10+)
- CMake 3.20+
- CUDA 11+ (optional, for GPU acceleration)

### Optional Dependencies

| Library | Purpose              | Detection                                      |
| ------- | -------------------- | ---------------------------------------------- |
| CBLAS   | GEMM acceleration    | Auto-detected via `__has_include(<cblas.h>)`   |
| LAPACKE | Inverse/determinant  | Auto-detected via `__has_include(<lapacke.h>)` |
| CUDA    | Batch GPU operations | Auto-detected via CMake                        |

When BLAS/LAPACK are not available, naive fallback implementations are used.

### Linking

```cmake
target_link_libraries(your_target PRIVATE utilities_math_linalg)
```

---

## 7. Testing

Run tests using the standard Docker workflow:

```bash
# Build
docker compose run --rm -T dev-cuda make debug

# Run linalg tests
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -L linalg
```

### Test Organization

| Test File             | Coverage                               | Tests |
| --------------------- | -------------------------------------- | ----- |
| `ArrayBase_uTest.cpp` | ArrayBase, ArrayOps, norms             | 16    |
| `Array_uTest.cpp`     | GEMM, transpose, inverse, det/trace    | 17    |
| `Vector_uTest.cpp`    | Vector operations, abs, min, max, etc. | 13    |
| `Matrix2_uTest.cpp`   | 2x2 matrix operations                  | 12    |
| `Matrix3_uTest.cpp`   | 3x3 matrix operations                  | 6     |
| `Matrix4_uTest.cpp`   | 4x4 matrix operations                  | 10    |
| `Rotations_uTest.cpp` | Euler, axis-angle, DCM conversions     | 15    |
| `ArrayCuda_uTest.cu`  | CUDA batch operations                  | 6     |

### Expected Output

```
100% tests passed, 0 tests failed out of 95
```

---

## 8. See Also

- `src/utilities/compatibility/inc/compat_blas.hpp` - BLAS/LAPACK compatibility layer
- `src/utilities/math/quaternion/` - Quaternion operations (CPU + CUDA)
- `src/utilities/math/integration/` - ODE integration (uses Quaternion for state)
- `src/utilities/math/legendre/` - Legendre polynomials (CPU + CUDA pattern)
