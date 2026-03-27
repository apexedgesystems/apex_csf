#ifndef APEX_UTILITIES_MATH_LEGENDRE_FACTORIALS_HPP
#define APEX_UTILITIES_MATH_LEGENDRE_FACTORIALS_HPP
/**
 * @file Factorials.hpp
 * @brief Factorial precomputation and lookup utilities for Legendre polynomial evaluation.
 *
 * Design goals:
 *  - Zero-allocation lookup via span overload
 *  - O(1) table lookup for precomputed range
 *  - Graceful extension beyond precomputed range
 */

#include <cstdint>
#include <vector>

#include "src/utilities/compatibility/inc/compat_span.hpp"

namespace apex {
namespace math {
namespace legendre {

/* ----------------------------- API ----------------------------- */

/**
 * @brief Build a lookup table of factorials from 0! to maxValue!.
 *
 * Negative maxValue is treated as zero, yielding a table of size 1 with only 0! = 1.
 *
 * @param maxValue Upper bound (inclusive) for precomputed factorials.
 * @return Vector F where F[i] == i! for i in [0, maxValue].
 * @note NOT RT-safe: Allocates std::vector.
 */
[[nodiscard]] std::vector<double> precomputedFactorials(int maxValue) noexcept;

/**
 * @brief Compute m! by lookup and extend if m >= table size.
 *
 * If m < factorials.size(), returns factorials[m]; otherwise
 * continues multiplying from factorials.back()+1 up to m.
 *
 * @param m          Index whose factorial is required.
 * @param factorials Table from precomputedFactorials().
 * @return m! as a double.
 * @note RT-safe: Zero-allocation with pre-built table.
 */
[[nodiscard]] double largeFactorial(std::uint32_t m,
                                    const std::vector<double>& factorials) noexcept;

/**
 * @brief Compute m! via a read-only view (no allocations).
 *
 * Behaves like the vector overload but takes a compatibility span of doubles.
 * Works on C++17 (shim) and maps to std::span<const double> on C++20+.
 *
 * @param m          Index whose factorial is required.
 * @param factorials Read-only view from precomputedFactorials() or caller-owned table.
 * @return m! as a double.
 * @note RT-safe: Zero-allocation with pre-built table.
 */
[[nodiscard]] double largeFactorial(std::uint32_t m,
                                    apex::compat::rospan<double> factorials) noexcept;

} // namespace legendre
} // namespace math
} // namespace apex

#endif // APEX_UTILITIES_MATH_LEGENDRE_FACTORIALS_HPP
