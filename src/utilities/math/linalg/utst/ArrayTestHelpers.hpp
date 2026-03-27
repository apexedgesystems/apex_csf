/**
 * @file ArrayTestHelpers.hpp
 * @brief Shared helpers for Array unit tests:
 *        - Tight ArrayBase/Array views over std::vector buffers
 *        - Simple host-side fillers
 *        - Status/tolerance utilities and matrix comparisons
 *
 * Design:
 *  - Header-only, inline-only (no global state)
 *  - Minimal dependencies; intended for use in both CPU and CUDA test TUs
 *  - These helpers are *host-only*. CUDA-specific helpers live in
 *    ArrayTestHelpers.cuh and must not redefine anything declared here.
 */

#ifndef APEX_MATH_LINALG_ARRAY_TEST_HELPERS_HPP
#define APEX_MATH_LINALG_ARRAY_TEST_HELPERS_HPP

#include "src/utilities/math/linalg/inc/Array.hpp"
#include "src/utilities/math/linalg/inc/ArrayBase.hpp"
#include "src/utilities/math/linalg/inc/ArrayStatus.hpp"

#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <initializer_list>
#include <type_traits>
#include <vector>

namespace apex {
namespace math {
namespace linalg {
namespace test {

/* =============================================================================
 * ArrayBase helpers (non-owning views over std::vector)
 * =============================================================================*/

/**
 * @brief Make a tight row-major view (ld = cols) over a std::vector buffer.
 *
 * @tparam T   element type
 * @param buf  underlying storage (must have at least rows*cols elements)
 * @param rows number of rows
 * @param cols number of columns
 * @return ArrayBase<T, void> view; ownership stays with @p buf
 *
 * @note No bounds checks here; use in tests where sizes are controlled.
 */
template <typename T>
inline ArrayBase<T, void> makeRowMajorView(std::vector<T>& buf, std::size_t rows,
                                           std::size_t cols) {
  return ArrayBase<T, void>(buf.data(), rows, cols, Layout::RowMajor, /*ld=*/cols);
}

/**
 * @brief Make a tight col-major view (ld = rows) over a std::vector buffer.
 *
 * @tparam T   element type
 * @param buf  underlying storage (must have at least rows*cols elements)
 * @param rows number of rows
 * @param cols number of columns
 * @return ArrayBase<T, void> view; ownership stays with @p buf
 */
template <typename T>
inline ArrayBase<T, void> makeColMajorView(std::vector<T>& buf, std::size_t rows,
                                           std::size_t cols) {
  return ArrayBase<T, void>(buf.data(), rows, cols, Layout::ColMajor, /*ld=*/rows);
}

/**
 * @brief Fill a contiguous vector with an arithmetic progression.
 *
 * @tparam T     element type
 * @param buf    buffer to fill
 * @param start  first value (default 1)
 * @param inc    increment per element (default 1)
 */
template <typename T>
inline void fillSequential(std::vector<T>& buf, T start = T(1), T inc = T(1)) {
  T v = start;
  for (auto& x : buf) {
    x = v;
    v = static_cast<T>(v + inc);
  }
}

/* =============================================================================
 * Array helpers (tight views and simple fillers)
 * =============================================================================*/

/**
 * @brief Row-major tight view (ld inferred) over contiguous storage for Array<T>.
 *
 * @tparam T   element type
 * @param buf  underlying storage (size >= rows*cols)
 * @param rows number of rows
 * @param cols number of columns
 */
template <typename T>
inline Array<T> makeRowView(std::vector<T>& buf, std::size_t rows, std::size_t cols) {
  return Array<T>(buf.data(), rows, cols, Layout::RowMajor, /*ld=*/0);
}

/**
 * @brief Col-major tight view (ld inferred) over contiguous storage for Array<T>.
 *
 * @tparam T   element type
 * @param buf  underlying storage (size >= rows*cols)
 * @param rows number of rows
 * @param cols number of columns
 */
template <typename T>
inline Array<T> makeColView(std::vector<T>& buf, std::size_t rows, std::size_t cols) {
  return Array<T>(buf.data(), rows, cols, Layout::ColMajor, /*ld=*/0);
}

/**
 * @brief Fill an Array view with A(i,j) = start, start+1, ... in *row-major* notion.
 *
 * @tparam T    element type
 * @param v     Array view (any layout; indexing abstracts it)
 * @param start first value (default 1)
 */
template <typename T> inline void fillSequential(Array<T> v, T start = T(1)) {
  T x = start;
  for (std::size_t i = 0; i < v.rows(); ++i) {
    for (std::size_t j = 0; j < v.cols(); ++j) {
      v(i, j) = x;
      x = static_cast<T>(x + T(1));
    }
  }
}

/**
 * @brief Type-based absolute tolerance helper (double tighter than float).
 *
 * @tparam T element type (float/double)
 * @return absolute tolerance suitable for EXPECT_NEAR in tests
 */
template <typename T> constexpr T tol() {
  return std::is_same<T, double>::value ? static_cast<T>(1e-12) : static_cast<T>(1e-5);
}

/**
 * @brief Nearly-equal matrix compare with absolute tolerance.
 *
 * @tparam T element type
 * @param got   matrix under test
 * @param ref   reference matrix
 * @param absTol absolute tolerance (e.g., tol<T>())
 *
 * Fails with coordinates on mismatch.
 */
template <typename T>
inline void expectNearMatrix(const Array<T>& got, const Array<T>& ref, T absTol) {
  ASSERT_EQ(got.rows(), ref.rows());
  ASSERT_EQ(got.cols(), ref.cols());
  for (std::size_t i = 0; i < got.rows(); ++i) {
    for (std::size_t j = 0; j < got.cols(); ++j) {
      EXPECT_NEAR(got(i, j), ref(i, j), absTol) << " at (" << i << "," << j << ")";
    }
  }
}

/* =============================================================================
 * Status helpers
 * =============================================================================*/

/**
 * @brief Assert a uint8_t status equals a given Status enum value.
 *
 * @param st   actual status (uint8_t)
 * @param want desired Status enum
 */
inline void expectStatus(std::uint8_t st, Status want) {
  EXPECT_EQ(st, static_cast<std::uint8_t>(want));
}

/**
 * @brief Assert a uint8_t status equals one of the allowed Status codes.
 *
 * @param st     actual status (uint8_t)
 * @param wants  initializer list of acceptable Status values
 *
 * On failure, prints the set of allowed codes.
 */
inline void expectStatusOneOf(std::uint8_t st, std::initializer_list<Status> wants) {
  bool ok = false;
  for (Status w : wants) {
    if (st == static_cast<std::uint8_t>(w)) {
      ok = true;
      break;
    }
  }
  if (!ok) {
    ::testing::Message m;
    m << "Status " << static_cast<int>(st) << " not in {";
    bool first = true;
    for (Status w : wants) {
      if (!first)
        m << ", ";
      first = false;
      m << static_cast<int>(static_cast<std::uint8_t>(w));
    }
    m << "}";
    ADD_FAILURE() << m.GetString();
  }
}

} // namespace test
} // namespace linalg
} // namespace math
} // namespace apex

#endif // APEX_MATH_LINALG_ARRAY_TEST_HELPERS_HPP
