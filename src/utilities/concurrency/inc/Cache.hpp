#ifndef APEX_UTILITIES_CONCURRENCY_CACHE_HPP
#define APEX_UTILITIES_CONCURRENCY_CACHE_HPP
/**
 * @file Cache.hpp
 * @brief Cacheline-alignment wrapper to reduce false sharing.
 *
 * Provides a simple template wrapper that aligns contained data to
 * cache line boundaries (64 bytes), preventing false sharing between
 * adjacent atomic variables accessed by different threads.
 */

#include <cstddef>

namespace apex {
namespace concurrency {
namespace cache {

/* ----------------------------- Constants ----------------------------- */

/// Typical cache line size on modern x86/ARM processors.
constexpr std::size_t CACHE_LINE_SIZE = 64;

/* ----------------------------- AlignCl ----------------------------- */

/**
 * @struct AlignCl
 * @brief Cacheline-aligned wrapper for reducing false sharing.
 * @tparam T Contained type.
 * @note RT-safe: Zero overhead wrapper, compile-time alignment only.
 */
template <class T> struct alignas(CACHE_LINE_SIZE) AlignCl {
  T value; ///< Wrapped value, aligned to cache line boundary.
};

} // namespace cache
} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_CACHE_HPP
