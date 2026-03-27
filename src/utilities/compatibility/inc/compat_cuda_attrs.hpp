#ifndef APEX_UTILITIES_COMPATIBILITY_CUDA_ATTRS_HPP
#define APEX_UTILITIES_COMPATIBILITY_CUDA_ATTRS_HPP

/**
 * @file compat_cuda_attrs.hpp
 * @brief CUDA attribute shims that compile cleanly with or without CUDA.
 *
 * These macros let public headers annotate device-capable functions without
 * leaking raw CUDA attributes. When CUDA is not available, they collapse to no-ops.
 *
 * - COMPAT_CUDA_AVAILABLE : 1 if compiling under NVCC/Clang-CUDA, else 0
 * - SIM_HOST              : __host__            (CUDA) / empty (CPU-only)
 * - SIM_HD                : __host__ __device__ (CUDA) / empty (CPU-only)
 * - SIM_D                 : __device__          (CUDA) / empty (CPU-only)
 * - SIM_FI                : __forceinline__     (CUDA) / inline (CPU-only)
 * - SIM_HD_FI             : __host__ __device__ __forceinline__ (CUDA) / inline (CPU-only)
 * - SIM_ALIGN(N)          : __align__(N)        (CUDA) / alignas(N) (CPU-only)
 * - SIM_CONSTANT          : __constant__        (CUDA) / empty (CPU-only)
 * - SIM_SHARED            : __shared__          (CUDA) / empty (CPU-only)
 * - SIM_RESTRICT          : __restrict__        (CUDA) / empty (CPU-only)
 * - SIM_NODISCARD         : [[nodiscard]] if available, else empty
 * - SIM_LIKELY(x) / SIM_UNLIKELY(x) : branch prediction hints
 */

#if defined(__CUDACC__)
#define COMPAT_CUDA_AVAILABLE 1
#else
#define COMPAT_CUDA_AVAILABLE 0
#endif

/* ------------------------ Base Attribute Shims ------------------------- */
#if COMPAT_CUDA_AVAILABLE
#define SIM_HOST __host__
#define SIM_HD __host__ __device__
#define SIM_D __device__
#define SIM_FI __forceinline__
#define SIM_ALIGN(N) __align__(N)
#define SIM_CONSTANT __constant__
#define SIM_SHARED __shared__
#else
#define SIM_HOST
#define SIM_HD
#define SIM_D
#if defined(_MSC_VER)
#define SIM_FI __forceinline
#else
#define SIM_FI inline
#endif
#define SIM_ALIGN(N) alignas(N)
#define SIM_CONSTANT
#define SIM_SHARED
#endif

// Convenience combo: host+device+forceinline (or inline on CPU-only).
#define SIM_HD_FI SIM_HD SIM_FI

/* --------------------------- Extra Helpers --------------------------- */

// Restrict aliasing hint.
#if COMPAT_CUDA_AVAILABLE
#define SIM_RESTRICT __restrict__
#else
#define SIM_RESTRICT
#endif

// [[nodiscard]] if available (C++17+), else empty.
#if defined(__has_cpp_attribute)
#if __has_cpp_attribute(nodiscard)
#define SIM_NODISCARD [[nodiscard]]
#else
#define SIM_NODISCARD
#endif
#else
#define SIM_NODISCARD
#endif

// Branch prediction hints.
#if defined(__GNUC__) || defined(__clang__)
#define SIM_LIKELY(x) __builtin_expect(!!(x), 1)
#define SIM_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define SIM_LIKELY(x) (x)
#define SIM_UNLIKELY(x) (x)
#endif

#endif // APEX_UTILITIES_COMPATIBILITY_CUDA_ATTRS_HPP
