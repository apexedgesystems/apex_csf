#ifndef APEX_UTILITIES_COMPATIBILITY_CUDA_DETECT_HPP
#define APEX_UTILITIES_COMPATIBILITY_CUDA_DETECT_HPP

/**
 * @file compat_cuda_detect.hpp
 * @brief Lightweight CUDA version/architecture feature detection helpers.
 *
 * Macros to gate features without sprinkling raw __CUDA_ARCH__ checks.
 *
 * - COMPAT_CUDA_VERSION          : e.g., 12040 for CUDA 12.4 (0 when unavailable)
 * - COMPAT_CUDA_ARCH             : raw __CUDA_ARCH__ value (SM*10) or 0 on host
 * - COMPAT_CUDA_SM               : SM as (major*100 + minor*10), e.g., 890 for 8.9 (0 on host)
 * - COMPAT_CUDA_SM_MAJOR         : SM major (0 on host)
 * - COMPAT_CUDA_SM_MINOR         : SM minor (0 on host)
 * - COMPAT_CUDA_ARCH_AT_LEAST(M,m): true iff device arch >= sm_Mm
 * - COMPAT_CUDA_HAS_FP64         : 1 if device arch supports double, else 0 (host: 1)
 * - COMPAT_CUDA_HAS_64B_ATOMICS  : 1 if 64-bit atomics likely supported (sm_60+), else 0 (host: 1)
 * - SIM_WARP_SIZE                : 32 (constant)
 */

#include "compat_cuda_attrs.hpp"

/* --------------------------- Version Macros --------------------------- */

// CUDA Toolkit version (compile-time)
#ifndef COMPAT_CUDA_VERSION
#if defined(CUDA_VERSION)
#define COMPAT_CUDA_VERSION CUDA_VERSION
#else
#define COMPAT_CUDA_VERSION 0
#endif
#endif

// Raw architecture macro (device compile only)
#if defined(__CUDA_ARCH__)
#define COMPAT_CUDA_ARCH __CUDA_ARCH__
#else
#define COMPAT_CUDA_ARCH 0
#endif

/* __CUDA_ARCH__ encodes SM as (major*100 + minor*10):
 *   sm_75 -> 750, sm_86 -> 860, sm_89 -> 890, etc.
 */
#if defined(__CUDA_ARCH__)
#define COMPAT_CUDA_SM (__CUDA_ARCH__)
#define COMPAT_CUDA_SM_MAJOR (__CUDA_ARCH__ / 100)
#define COMPAT_CUDA_SM_MINOR ((__CUDA_ARCH__ % 100) / 10)
#else
#define COMPAT_CUDA_SM 0
#define COMPAT_CUDA_SM_MAJOR 0
#define COMPAT_CUDA_SM_MINOR 0
#endif

/* ------------------------- Architecture Gates ------------------------- */

// Convenience gate: is the current device arch at least sm_{M}{m}?
// Example: COMPAT_CUDA_ARCH_AT_LEAST(8, 9) -> sm_89 or newer.
#if defined(__CUDA_ARCH__)
#define COMPAT_CUDA_ARCH_AT_LEAST(MAJ, MIN) (__CUDA_ARCH__ >= ((MAJ) * 100 + (MIN) * 10))
#else
#define COMPAT_CUDA_ARCH_AT_LEAST(MAJ, MIN) 0
#endif

// Double precision availability: sm_13+ historically. Modern GPUs all pass this.
#if defined(__CUDA_ARCH__)
#if __CUDA_ARCH__ >= 130
#define COMPAT_CUDA_HAS_FP64 1
#else
#define COMPAT_CUDA_HAS_FP64 0
#endif
#else
// On host, 'double' exists; device checks happen in device code paths.
#define COMPAT_CUDA_HAS_FP64 1
#endif

// 64-bit atomics broadly from sm_60+.
#if defined(__CUDA_ARCH__)
#if __CUDA_ARCH__ >= 600
#define COMPAT_CUDA_HAS_64B_ATOMICS 1
#else
#define COMPAT_CUDA_HAS_64B_ATOMICS 0
#endif
#else
// Host-side code can assume availability; device code must gate appropriately.
#define COMPAT_CUDA_HAS_64B_ATOMICS 1
#endif

/* -------------------------- Hardware Constants ------------------------- */

// Warp size: 32 on all current NVIDIA architectures.
#define SIM_WARP_SIZE 32

#endif // APEX_UTILITIES_COMPATIBILITY_CUDA_DETECT_HPP
