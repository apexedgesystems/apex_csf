/**
 * @file Factorials.cpp
 * @brief Implementation of factorial precomputation and lookup utilities.
 */

#include "src/utilities/math/legendre/inc/Factorials.hpp"

#include <cstddef>
#include <vector>

namespace apex {
namespace math {
namespace legendre {

/* ----------------------------- File Helpers ----------------------------- */

namespace {

// Zero-alloc precompute into caller buffer.
inline void precomputedFactorialsNoalloc(int maxValue, double* out, std::size_t bufSize) noexcept {
  if (!out || bufSize == 0) {
    return;
  }

  const int MAX_V = (maxValue > 0 ? maxValue : 0);
  const std::size_t NEED = static_cast<std::size_t>(MAX_V) + 1u;
  const std::size_t LIMIT = (bufSize < NEED) ? bufSize : NEED;

  out[0] = 1.0;
  for (std::size_t i = 1; i < LIMIT; ++i) {
    out[i] = out[i - 1] * static_cast<double>(i);
  }
}

// Zero-alloc factorial via pointer lookup + extend.
inline double largeFactorialPtr(std::uint32_t m, const double* table,
                                std::size_t tableSize) noexcept {
  if (table && m < tableSize) {
    return table[m];
  }

  // Start from last known factorial, or 1.0 if table empty.
  double result = (table && tableSize > 0) ? table[tableSize - 1] : 1.0;

  // If tableSize == 0, begin at 1 to avoid multiplying by 0.
  std::uint32_t i0 = (tableSize > 0)
                         ? static_cast<std::uint32_t>(tableSize) // next after (tableSize-1)!
                         : 1u;                                   // start at 1! when empty

  for (std::uint32_t i = i0; i <= m; ++i) {
    result *= static_cast<double>(i);
  }
  return result;
}

} // namespace

/* ----------------------------- API ----------------------------- */

std::vector<double> precomputedFactorials(int maxValue) noexcept {
  const int MAX_V = (maxValue > 0 ? maxValue : 0);
  std::vector<double> table(static_cast<std::size_t>(MAX_V) + 1u);
  precomputedFactorialsNoalloc(MAX_V, table.data(), table.size());
  return table;
}

double largeFactorial(std::uint32_t m, const std::vector<double>& factorials) noexcept {
  return largeFactorialPtr(m, factorials.data(), factorials.size());
}

double largeFactorial(std::uint32_t m, apex::compat::rospan<double> factorials) noexcept {
  return largeFactorialPtr(m, factorials.data(), factorials.size());
}

} // namespace legendre
} // namespace math
} // namespace apex
