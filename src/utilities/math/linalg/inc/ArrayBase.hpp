#ifndef APEX_MATH_LINALG_ARRAY_BASE_HPP
#define APEX_MATH_LINALG_ARRAY_BASE_HPP
/**
 * @file ArrayBase.hpp
 * @brief Non-owning 2D array view with explicit layout and leading dimension.
 *
 * Zero-allocation, RT-friendly. Ownership is external; this class never
 * allocates or frees memory. Operators are preconditioned (like std::array):
 * callers must pass valid indices. Use the error-code APIs (get/set) for
 * bounds-checked access without exceptions.
 *
 * @note RT-SAFE: Zero allocation, noexcept throughout.
 */

#include "src/utilities/compatibility/inc/compat_blas.hpp"
#include "src/utilities/math/linalg/inc/ArrayStatus.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace apex {
namespace math {
namespace linalg {

/* --------------------------------- Types ---------------------------------- */

/// Memory layout tag for BLAS/LAPACK interop and indexing.
using Layout = compat::blas::Layout;
using Transpose = compat::blas::Transpose;

/* ------------------------------- ArrayBase -------------------------------- */

/**
 * @brief Non-owning 2D array view with explicit memory layout.
 *
 * @tparam T Element type (should be POD-like for BLAS/LAPACK/CUDA interop).
 * @tparam Derived CRTP derived type (default void for standalone use).
 *
 * @note RT-SAFE: Zero allocation, all methods noexcept.
 */
template <typename T, class Derived = void> class ArrayBase {
public:
  using ValueType = T;
  using SizeType = std::size_t;
  using Pointer = T*;
  using ConstPointer = const T*;

  /* --------------------------- Construction ------------------------------ */

  /**
   * @brief Construct a non-owning 2D view.
   *
   * @param data   Pointer to first element.
   * @param rows   Number of rows.
   * @param cols   Number of columns.
   * @param layout RowMajor or ColMajor (default RowMajor).
   * @param ld     Leading dimension in elements. If 0, inferred as tight.
   *
   * @note RT-SAFE: No allocation.
   */
  constexpr ArrayBase(T* data, std::size_t rows, std::size_t cols, Layout layout = Layout::RowMajor,
                      std::size_t ld = 0) noexcept;

  // Trivial copy/move (non-owning).
  constexpr ArrayBase(const ArrayBase&) noexcept = default;
  constexpr ArrayBase(ArrayBase&&) noexcept = default;
  constexpr ArrayBase& operator=(const ArrayBase&) noexcept = default;
  constexpr ArrayBase& operator=(ArrayBase&&) noexcept = default;
  ~ArrayBase() = default;

  /* ----------------------------- Accessors ------------------------------- */

  constexpr T* data() noexcept;
  constexpr const T* data() const noexcept;
  constexpr std::size_t rows() const noexcept;
  constexpr std::size_t cols() const noexcept;
  constexpr std::size_t ld() const noexcept;
  constexpr Layout layout() const noexcept;
  constexpr std::size_t size() const noexcept;
  constexpr bool isContiguous() const noexcept;

  /* -------------------- Preconditioned Element Access -------------------- */

  /** @brief Access element (no bounds check). RT-SAFE. */
  T& operator()(std::size_t r, std::size_t c) noexcept;
  const T& operator()(std::size_t r, std::size_t c) const noexcept;

  /** @brief Linear access (contiguous only, no bounds check). RT-SAFE. */
  T& operator[](std::size_t i) noexcept;
  const T& operator[](std::size_t i) const noexcept;

  /* ----------------------- Checked Accessors ----------------------------- */

  /** @brief Bounds-checked get. Returns status code. RT-SAFE. */
  uint8_t get(std::size_t r, std::size_t c, T& out) const noexcept;
  uint8_t set(std::size_t r, std::size_t c, const T& v) noexcept;

  /** @brief Linear bounds-checked access (contiguous only). RT-SAFE. */
  uint8_t get(std::size_t i, T& out) const noexcept;
  uint8_t set(std::size_t i, const T& v) noexcept;

  /* ------------------------- Layout Control ------------------------------ */

  /** @brief Toggle layout (only if contiguous). RT-SAFE. */
  uint8_t setLayout(Layout newLayout) noexcept;

  /** @brief View-only transpose (swap rows/cols, flip layout). RT-SAFE. */
  ArrayBase<T, Derived> transposeView() const noexcept;

  /* ----------------------------- Utilities ------------------------------- */

  /** @brief Swap metadata with another view. RT-SAFE. */
  void swap(ArrayBase& other) noexcept;

private:
  T* data_;
  std::size_t rows_;
  std::size_t cols_;
  std::size_t ld_;
  Layout layout_;
};

/* --------------------------- Free Functions ------------------------------ */

/** @brief Compare two arrays: same shape + element-wise equality. RT-SAFE. */
template <typename T, class D1, class D2>
bool compare(const ArrayBase<T, D1>& a, const ArrayBase<T, D2>& b) noexcept;

} // namespace linalg
} // namespace math
} // namespace apex

#include "src/utilities/math/linalg/src/ArrayBase.tpp"

#endif // APEX_MATH_LINALG_ARRAY_BASE_HPP
