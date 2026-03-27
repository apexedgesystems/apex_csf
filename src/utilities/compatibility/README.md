# Compatibility Library

**Namespace:** `apex::compat::`
**Platform:** Cross-platform (Linux, macOS, Windows)
**C++ Standard:** C++17 (C++20/C++23 for native implementations)

Header-only compatibility shims providing portability across C++ standards, OpenSSL versions, and GPU builds.

---

## Table of Contents

1. [Quick Reference](#1-quick-reference)
2. [Design Principles](#2-design-principles)
3. [Module Reference](#3-module-reference)
4. [Requirements](#4-requirements)
5. [Testing](#5-testing)
6. [See Also](#6-see-also)

---

## 1. Quick Reference

### Core Language (5 headers)

| Header                   | Purpose               | Key Features                                                 |
| ------------------------ | --------------------- | ------------------------------------------------------------ |
| `compat_attributes.hpp`  | Compiler hints        | `COMPAT_HOT`, `COMPAT_LIKELY`, `COMPAT_UNLIKELY`             |
| `compat_byteswap.hpp`    | Byte order conversion | `byteswap()`, `byteswap_ieee()` - C++23 shim                 |
| `compat_concurrency.hpp` | Atomic wait/notify    | `waitEq()`, `notifyOne/All()` - C++20 shim                   |
| `compat_endian.hpp`      | Endian detection      | `endian` enum, `NATIVE_ENDIAN` - C++20 shim                  |
| `compat_span.hpp`        | Byte views            | `bytes_span`, `mutable_bytes_span`, `rospan<T>` - C++20 shim |

### CUDA Integration (5 headers)

| Header                   | Purpose                | Key Features                                                 |
| ------------------------ | ---------------------- | ------------------------------------------------------------ |
| `compat_cuda_attrs.hpp`  | Device annotations     | `SIM_HD`, `SIM_D`, `SIM_FI` - Compile to no-ops without CUDA |
| `compat_cuda_detect.hpp` | Version/arch detection | `COMPAT_CUDA_VERSION`, `COMPAT_CUDA_ARCH_AT_LEAST()`         |
| `compat_cuda_error.hpp`  | Error handling         | `COMPAT_CUDA_CHECK()`, `g_error_callback` (RT-safe)          |
| `compat_cuda_memory.hpp` | Pinned memory          | `pinned_buffer<T>` - Falls back to `std::vector`             |
| `compat_nvml_detect.hpp` | NVML detection         | Header availability, missing constant shims                  |

### Library Wrappers (1 header)

| Header               | Purpose              | Key Features                                    |
| -------------------- | -------------------- | ----------------------------------------------- |
| `compat_openssl.hpp` | OpenSSL 1.1.1 vs 3.x | Provider loading, algorithm fetch, RAII handles |

### BLAS/LAPACK Shims (2 headers)

| Header                 | Purpose                  | Key Features                                     |
| ---------------------- | ------------------------ | ------------------------------------------------ |
| `compat_blas.hpp`      | CBLAS/LAPACKE wrappers   | Layout enum, `toCblasLayout()`, `makeGemmDims()` |
| `compat_cuda_blas.hpp` | cuBLAS/cuSOLVER wrappers | Handle management, status mapping, GEMM helpers  |

### Basic Usage

```cpp
#include "compat_attributes.hpp"
#include "compat_byteswap.hpp"
#include "compat_span.hpp"

void processPacket() COMPAT_HOT {
  if (COMPAT_LIKELY(packetValid)) {
    uint32_t net_order = apex::compat::byteswap(host_value);
    transmit(apex::compat::bytes_span(buffer));
  }
}
```

---

## 2. Design Principles

### RT-Safety Annotations

Every header documents its real-time safety:

| Annotation     | Meaning                                              |
| -------------- | ---------------------------------------------------- |
| **RT-SAFE**    | Deterministic, no syscalls, no allocations, no locks |
| **RT-CAUTION** | May have syscalls or one-time initialization         |
| **RT-DEPENDS** | Safety depends on build config or runtime path       |

### RT-Safety Overview

| Header                   | RT Status  | Reason                    | Notes                          |
| ------------------------ | ---------- | ------------------------- | ------------------------------ |
| `compat_attributes.hpp`  | RT-SAFE    | Compile-time macros       | Zero runtime cost              |
| `compat_byteswap.hpp`    | RT-SAFE    | constexpr functions       | Zero cost on C++23             |
| `compat_endian.hpp`      | RT-SAFE    | Compile-time detection    | Zero runtime cost              |
| `compat_span.hpp`        | RT-SAFE    | Inline accessors          | Zero-cost wrapper              |
| `compat_cuda_attrs.hpp`  | RT-SAFE    | Compile-time macros       | Zero runtime cost              |
| `compat_cuda_detect.hpp` | RT-SAFE    | Compile-time macros       | Zero runtime cost              |
| `compat_nvml_detect.hpp` | RT-SAFE    | Header detection only     | Compile-time macros            |
| `compat_concurrency.hpp` | RT-CAUTION | Syscalls on C++17         | C++20 uses `std::atomic::wait` |
| `compat_openssl.hpp`     | RT-CAUTION | One-time `std::call_once` | Pre-load in init               |
| `compat_cuda_error.hpp`  | RT-DEPENDS | Depends on NDEBUG         | Safe in release mode           |
| `compat_cuda_memory.hpp` | RT-DEPENDS | Depends on fallback       | Safe with pinned memory        |
| `compat_blas.hpp`        | RT-SAFE    | constexpr functions       | Pure dimension helpers         |
| `compat_cuda_blas.hpp`   | RT-DEPENDS | Handle ops are RT-UNSAFE  | Status mapping is RT-SAFE      |

### Recommendations for RT Systems

1. **Use C++20** when possible for optimal RT performance
2. **Pre-initialize** in non-RT setup phase:

   ```cpp
   apex::compat::ossl::ensureProvidersLoaded();  // OpenSSL
   ```

3. **Build in Release mode** (`-DNDEBUG`) for production RT
4. **Profile before deploying**: `strace -c ./your_app`

---

## 3. Module Reference

### Attributes

**Header:** `compat_attributes.hpp`
**RT-Safety:** RT-SAFE

Compiler portability macros for hot-path and branch prediction hints.

```cpp
#include "compat_attributes.hpp"

int processPacket() COMPAT_HOT;

if (COMPAT_LIKELY(packetValid)) {
  // fast path
}
```

### Endian Detection

**Header:** `compat_endian.hpp`
**RT-Safety:** RT-SAFE

Compile-time endianness detection with C++20 `std::endian` shim.

```cpp
#include "compat_endian.hpp"

if constexpr (apex::compat::NATIVE_ENDIAN == apex::compat::endian::little) {
  // Little-endian specific code
}
```

### Byte-Swap Helpers

**Header:** `compat_byteswap.hpp`
**RT-Safety:** RT-SAFE

Portable, constexpr-capable byte-swap for integral and IEEE-754 types.

```cpp
#include "compat_byteswap.hpp"

uint32_t x = 0x11223344u;
auto y = apex::compat::byteswap(x);  // y == 0x44332211

double d = 123.456;
auto swapped = apex::compat::byteswap_ieee(d);
```

### Span Shim

**Header:** `compat_span.hpp`
**RT-Safety:** RT-SAFE

Byte views that resolve to `std::span` on C++20+ or lightweight shims on C++17.

| Type                 | Description                                 |
| -------------------- | ------------------------------------------- |
| `bytes_span`         | Read-only view (`std::span<const uint8_t>`) |
| `mutable_bytes_span` | Writable view (`std::span<uint8_t>`)        |
| `rospan<T>`          | Generic read-only view for any type T       |

```cpp
#include "compat_span.hpp"

void transmit(apex::compat::bytes_span data);              // read-only
void receive(apex::compat::mutable_bytes_span buffer);     // writable

std::vector<uint8_t> buf{0x01, 0x02, 0x03};
transmit(buf);  // implicit bytes_span construction
receive(buf);   // implicit mutable_bytes_span construction
```

### Concurrency Helpers

**Header:** `compat_concurrency.hpp`
**RT-Safety:** RT-CAUTION (syscalls on C++17)

Atomic wait/notify operations with C++20 fast-path and C++17 fallback.

```cpp
#include "compat_concurrency.hpp"

std::atomic<int> status{0};

// Thread 1: Wait for status change
apex::compat::atom::waitEq(status, 0);

// Thread 2: Update and notify
status.store(1, std::memory_order_release);
apex::compat::atom::notifyOne(status);
```

**Performance:**

| C++ Version      | Implementation      | Latency | RT-Safe?            |
| ---------------- | ------------------- | ------- | ------------------- |
| C++20            | `std::atomic::wait` | 10-50ns | Yes                 |
| C++17 + futex    | Linux futex syscall | ~100ns  | Caution             |
| C++17 + spinlock | Exponential backoff | 0ns     | Yes (CPU-intensive) |

### OpenSSL Helpers

**Header:** `compat_openssl.hpp`
**RT-Safety:** RT-CAUTION (one-time initialization)

Version-safe utilities for OpenSSL 1.1.1 and 3.x with RAII handles.

```cpp
#include "compat_openssl.hpp"

struct Sha256 {
  static constexpr const char* fetchHashName() { return "SHA256"; }
  static const EVP_MD* fetchHashAlgorithm() { return EVP_sha256(); }
};

auto md = apex::compat::ossl::fetchMd<Sha256>();
if (md.md) {
  // use with EVP_DigestInit_ex/Update/Final
}
```

### CUDA Shims

**Headers:** `compat_cuda_attrs.hpp`, `compat_cuda_detect.hpp`, `compat_cuda_error.hpp`, `compat_cuda_memory.hpp`
**RT-Safety:** Varies (see Design Principles)

Header-only macros and helpers for CUDA builds. Compile to no-ops in CPU-only builds.

```cpp
#include "compat_cuda_attrs.hpp"
#include "compat_cuda_detect.hpp"

SIM_HD_FI inline std::size_t triIndex(int n, int m) noexcept {
  return static_cast<std::size_t>(n) * static_cast<std::size_t>(n + 1) / 2
       + static_cast<std::size_t>(m);
}

#if COMPAT_CUDA_AVAILABLE
__global__ void kernel(double* SIM_RESTRICT out) {
  // device code...
}
#endif
```

**RT-safe error handling:**

```cpp
#include "compat_cuda_error.hpp"

// At startup (non-RT):
apex::compat::cuda::g_error_callback = [](const char* f, int l, const char* m) noexcept {
  myRtSafeLogger(f, l, m);
};

// In RT code:
COMPAT_CUDA_CHECK(cudaMalloc(&ptr, size));
```

### NVML Detection

**Header:** `compat_nvml_detect.hpp`
**RT-Safety:** RT-SAFE

NVML availability detection with shims for missing constants.

```cpp
#include "compat_nvml_detect.hpp"

#if COMPAT_NVML_AVAILABLE
#include <nvml.h>

void queryGpu() {
  nvmlDevice_t device;
  nvmlDeviceGetHandleByIndex(0, &device);
  // Use shim constants (work on old and new NVML)
}
#endif
```

### BLAS/LAPACK Helpers

**Header:** `compat_blas.hpp`
**RT-Safety:** RT-SAFE

Layout-agnostic BLAS/LAPACK dimension helpers with CBLAS/LAPACKE header detection.

```cpp
#include "compat_blas.hpp"

using apex::compat::blas::Layout;
using apex::compat::blas::tightLd;

// Compute tight leading dimension
std::size_t ld = tightLd(rows, cols, Layout::RowMajor);

// Map to CBLAS layout constant
int cblasLayout = apex::compat::blas::toCblasLayout(Layout::RowMajor);
```

### cuBLAS/cuSOLVER Helpers

**Header:** `compat_cuda_blas.hpp`
**RT-Safety:** RT-DEPENDS (Handle creation is RT-UNSAFE, status mapping is RT-SAFE)

CUDA BLAS library shims with row-major emulation for cuBLAS (column-major native).

```cpp
#include "compat_cuda_blas.hpp"

// Check availability at compile time
if constexpr (apex::compat::cuda::cublasAvailable()) {
  apex::compat::cuda::BlasHandle handle;
  apex::compat::cuda::createBlasHandle(&handle);  // RT-UNSAFE: setup only

  // Compute GEMM dimensions with row-major emulation
  apex::compat::cuda::GemmDims dims;
  apex::compat::cuda::makeGemmDims(aRows, aCols, lda, bRows, bCols, ldb,
                                    cRows, cCols, ldc, Layout::RowMajor,
                                    Transpose::NoTrans, Transpose::NoTrans, dims);
}
```

---

## 4. Requirements

### C++ Standard

| Standard | Status      | Notes                                   |
| -------- | ----------- | --------------------------------------- |
| C++17    | Minimum     | All features available with fallbacks   |
| C++20    | Recommended | Native `std::span`, `std::atomic::wait` |
| C++23    | Full        | Native `std::byteswap`                  |

### Compiler Support

| Compiler | Minimum | C++17 | C++20  | C++23  |
| -------- | ------- | ----- | ------ | ------ |
| GCC      | 7.0     | Yes   | 10.0+  | 11.0+  |
| Clang    | 5.0     | Yes   | 10.0+  | 16.0+  |
| MSVC     | 19.14   | Yes   | 19.25+ | 19.30+ |
| NVCC     | 10.0    | Yes   | 11.0+  | 12.0+  |

### Optional Dependencies

| Dependency   | Minimum | Notes                      |
| ------------ | ------- | -------------------------- |
| OpenSSL      | 1.1.1   | For `compat_openssl.hpp`   |
| CUDA Toolkit | 10.0    | For CUDA shims             |
| NVML         | Any     | For GPU telemetry          |
| CBLAS        | Any     | For `compat_blas.hpp`      |
| LAPACKE      | Any     | For `compat_blas.hpp`      |
| cuBLAS       | 10.0    | For `compat_cuda_blas.hpp` |
| cuSOLVER     | 10.0    | For `compat_cuda_blas.hpp` |

### Compatibility Matrix

| Feature                            | C++17          | C++20    | C++23  |
| ---------------------------------- | -------------- | -------- | ------ |
| `apex::compat::bytes_span`         | Shim           | Native   | Native |
| `apex::compat::mutable_bytes_span` | Shim           | Native   | Native |
| `apex::compat::byteswap`           | Fallback       | Fallback | Native |
| `apex::compat::atom::waitEq`       | Futex/spinlock | Native   | Native |
| CUDA shims                         | Active         | Active   | Active |

---

## 5. Testing

Run tests using the standard Docker workflow:

```bash
# Build first
docker compose run --rm -T dev-cuda make debug

# Run all compatibility tests
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -L compatibility
```

### Test Organization

| Module      | Test File                      | Tests |
| ----------- | ------------------------------ | ----- |
| Endian      | `compat_endian_uTest.cpp`      | 5     |
| Byteswap    | `compat_byteswap_uTest.cpp`    | 9     |
| Span        | `compat_span_uTest.cpp`        | 13    |
| Concurrency | `compat_concurrency_uTest.cpp` | 8     |

**Note:** CUDA tests require CUDA-enabled builds. OpenSSL tests require OpenSSL availability.

### Performance

Byteswap throughput on x86-64 (batch of 10,000, 15 repeats):

| Operation            | Median (us) | Per-Op (ns)    | CV%  |
| -------------------- | ----------- | -------------- | ---- |
| byteswap(uint16)     | 70.4        | 7.0            | 1.9% |
| byteswap(uint32)     | 57.7        | 5.8            | 1.9% |
| byteswap(uint64)     | 60.9        | 6.1            | 0.5% |
| byteswapIeee(double) | 105.7       | 10.6           | 1.0% |
| Endian detection     | 0.011 us    | (compile-time) | 5.1% |

Byteswap compiles to a single `bswap` instruction on x86. ByteswapIeee
adds memcpy round-trips that the compiler optimizes to register moves.
Endian detection is a compile-time constant with zero runtime cost.

---

## 6. See Also

- **utilities/diagnostics** - System diagnostics using these shims
- **docs/PRODUCTION_CODE_STANDARD.md** - Code standards
- **[GCC Function Attributes](https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html)** - `__attribute__((hot))`
- **[OpenSSL Provider API](https://www.openssl.org/docs/man3.0/man7/provider.html)** - Provider documentation
- **[std::span](https://en.cppreference.com/w/cpp/container/span)** - C++20 span reference
- **[std::byteswap](https://en.cppreference.com/w/cpp/numeric/byteswap)** - C++23 byteswap reference
- **[CUDA Programming Guide](https://docs.nvidia.com/cuda/cuda-c-programming-guide/index.html)** - NVIDIA documentation
