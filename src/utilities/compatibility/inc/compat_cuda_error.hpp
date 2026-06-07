#ifndef APEX_UTILITIES_COMPATIBILITY_CUDA_ERROR_HPP
#define APEX_UTILITIES_COMPATIBILITY_CUDA_ERROR_HPP
/**
 * @file compat_cuda_error.hpp
 * @brief Minimal error helpers for CUDA calls with CPU-only safe stubs.
 *
 * If CUDA runtime headers are available, exposes helpers using cudaError_t.
 * If not, provides stubs so code compiles without CUDA.
 *
 * RT-Safety:
 *  - Release builds (-DNDEBUG): RT-SAFE - COMPAT_CUDA_CHECK is a no-op
 *  - Debug builds: RT-DEPENDS - Default uses fprintf (RT-UNSAFE)
 *    * For RT systems: Set g_error_callback to your RT-safe error handler
 *    * Callback signature: void(const char* file, int line, const char* msg) noexcept
 *
 * Example (RT-safe error handling):
 *   // At application startup:
 *   apex::compat::cuda::g_error_callback = [](const char* f, int l, const char* m) noexcept {
 *     myRtSafeLogger(f, l, m);  // Your RT-safe logging system
 *   };
 *
 * Performance:
 *  - Release: 0ns overhead (compiled away)
 *  - Debug with callback: ~10ns (function call)
 *  - Debug with fprintf: ~1us (I/O syscall)
 *
 * NOTE: Deliberately does NOT define apex::compat::cuda::runtimeAvailable().
 * That symbol is owned by compat_cuda_blas.hpp to avoid ODR clashes.
 */

#include "compat_cuda_attrs.hpp"

#include <cstdint>
#if !defined(NDEBUG)
#include <cstdio>
#include <cstdlib>
#endif

// Detect header availability independently of device compilation.
#ifndef COMPAT_CUDA_HAS_RUNTIME_HEADERS
#if defined(__has_include)
#if __has_include(<cuda_runtime_api.h>)
#define COMPAT_CUDA_HAS_RUNTIME_HEADERS 1
#else
#define COMPAT_CUDA_HAS_RUNTIME_HEADERS 0
#endif
#else
#define COMPAT_CUDA_HAS_RUNTIME_HEADERS COMPAT_CUDA_AVAILABLE
#endif
#endif

// Pull CUDA headers at global namespace, never inside apex::compat::cuda.
#if COMPAT_CUDA_HAS_RUNTIME_HEADERS
#include <cuda_runtime_api.h>
#endif

namespace apex {
namespace compat {
namespace cuda {

#if COMPAT_CUDA_HAS_RUNTIME_HEADERS

/* ------------------------------ API Types ------------------------------ */

// Header-only availability flag (compile-time knowledge).
inline constexpr bool RUNTIME_HEADERS_AVAILABLE = true;

/**
 * @brief Human-readable error string for a CUDA error code.
 * @note RT-safe (no allocations, returns static string).
 */
inline const char* errorString(::cudaError_t e) noexcept { return ::cudaGetErrorString(e); }

/**
 * @brief Convert to status code (0 = success).
 * @note RT-safe (pure calculation).
 */
inline std::uint8_t toStatus(::cudaError_t e) noexcept { return (e == ::cudaSuccess) ? 0u : 1u; }

/**
 * @brief Boolean convenience.
 * @note RT-safe (pure calculation).
 */
inline bool isSuccess(::cudaError_t e) noexcept { return e == ::cudaSuccess; }

/* ------------------------- Error Callback (RT) ------------------------- */

// ---- Error Callback (optional, for RT systems) ----
// User-defined error handler. Called instead of fprintf in debug builds.
// Signature: void callback(const char* file, int line, const char* error_msg)
// Thread-safety: User's responsibility if setting callback from multiple threads
using ErrorCallback = void (*)(const char* file, int line, const char* error_msg) noexcept;

// Global error callback pointer. Default: nullptr (uses fprintf)
// Set this in your application initialization to override default error handling.
// Example:
//   apex::compat::cuda::g_error_callback = [](const char* f, int l, const char* m) noexcept {
//     myRtSafeLogger(f, l, m);  // Your RT-safe logging
//   };
inline ErrorCallback g_error_callback = nullptr;

/* --------------------------- Error Macros ---------------------------- */
#define COMPAT_CUDA_TRY_RETURN(expr)                                                               \
  do {                                                                                             \
    ::cudaError_t _e = (expr);                                                                     \
    if (_e != ::cudaSuccess) {                                                                     \
      return ::apex::compat::cuda::toStatus(_e);                                                   \
    }                                                                                              \
  } while (0)

#define COMPAT_CUDA_TRY_RETURN_BOOL(expr)                                                          \
  do {                                                                                             \
    ::cudaError_t _e = (expr);                                                                     \
    if (_e != ::cudaSuccess) {                                                                     \
      return false;                                                                                \
    }                                                                                              \
  } while (0)

#define COMPAT_CUDA_TRY_GUARD(expr, label)                                                         \
  do {                                                                                             \
    ::cudaError_t _e = (expr);                                                                     \
    if (_e != ::cudaSuccess) {                                                                     \
      goto label;                                                                                  \
    }                                                                                              \
  } while (0)

#if !defined(NDEBUG)
#define COMPAT_CUDA_CHECK(expr)                                                                    \
  do {                                                                                             \
    ::cudaError_t _e = (expr);                                                                     \
    if (_e != ::cudaSuccess) {                                                                     \
      if (::apex::compat::cuda::g_error_callback) {                                                \
        ::apex::compat::cuda::g_error_callback(__FILE__, __LINE__,                                 \
                                               ::apex::compat::cuda::errorString(_e));             \
      } else {                                                                                     \
        std::fprintf(stderr, "[CUDA ERROR] %s:%d: %s\n", __FILE__, __LINE__,                       \
                     ::apex::compat::cuda::errorString(_e));                                       \
      }                                                                                            \
      std::abort();                                                                                \
    }                                                                                              \
  } while (0)
#else
#define COMPAT_CUDA_CHECK(expr) (void)(expr)
#endif

#else // !COMPAT_CUDA_HAS_RUNTIME_HEADERS

/* ------------------------- CPU-Only Stubs ---------------------------- */

inline constexpr bool runtime_headers_available = false;

// CPU-only stubs so hosts can compile without CUDA headers.
enum class cuda_stub_error : int { success = 0, failed = 1 };

/**
 * @brief Human-readable error string (stub).
 * @note RT-safe (returns static string).
 */
inline const char* errorString(cuda_stub_error) noexcept { return "CUDA runtime not available"; }

/**
 * @brief Convert to status code (0 = success).
 * @note RT-safe (pure calculation).
 */
inline std::uint8_t toStatus(cuda_stub_error e) noexcept {
  return (e == cuda_stub_error::success) ? 0u : 1u;
}

/**
 * @brief Boolean convenience.
 * @note RT-safe (pure calculation).
 */
inline bool isSuccess(cuda_stub_error e) noexcept { return e == cuda_stub_error::success; }

// Stub macros compile and indicate failure so callers can skip/abort.
#define COMPAT_CUDA_TRY_RETURN(expr)                                                               \
  do {                                                                                             \
    (void)(expr);                                                                                  \
    return 1u;                                                                                     \
  } while (0)

#define COMPAT_CUDA_TRY_RETURN_BOOL(expr)                                                          \
  do {                                                                                             \
    (void)(expr);                                                                                  \
    return false;                                                                                  \
  } while (0)

#define COMPAT_CUDA_TRY_GUARD(expr, label)                                                         \
  do {                                                                                             \
    (void)(expr);                                                                                  \
    goto label;                                                                                    \
  } while (0)

#define COMPAT_CUDA_CHECK(expr) (void)(expr)

#endif // COMPAT_CUDA_HAS_RUNTIME_HEADERS

} // namespace cuda
} // namespace compat
} // namespace apex

#endif // APEX_UTILITIES_COMPATIBILITY_CUDA_ERROR_HPP