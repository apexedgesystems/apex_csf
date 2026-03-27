#ifndef APEX_MATH_LINALG_VECTOR_HPP
#define APEX_MATH_LINALG_VECTOR_HPP
/**
 * @file Vector.hpp
 * @brief Strong vector type (composition over Array) with vector-specific APIs.
 *
 * Design:
 *  - Wraps an Array<T> view and enforces vector shape (N x 1 or 1 x N).
 *  - Exposes vector-only operations (dot, norm, normalize, cross, projection).
 *  - Uses uint8_t status codes; no exceptions; no allocations.
 *
 * Conventions:
 *  - Orientation is explicit: Column (N x 1) or Row (1 x N).
 *
 * @note RT-SAFE: All operations noexcept, no allocations.
 */

#include "src/utilities/math/linalg/inc/Array.hpp"
#include "src/utilities/math/linalg/inc/ArrayBase.hpp"
#include "src/utilities/math/linalg/inc/ArrayStatus.hpp"

#include <cstddef>
#include <cstdint>

namespace apex {
namespace math {
namespace linalg {

/* ----------------------------- VectorOrient ------------------------------ */

/** @brief Explicit orientation of a vector. */
enum class VectorOrient : std::uint8_t {
  Col = 0, ///< Column vector (N x 1).
  Row = 1  ///< Row vector (1 x N).
};

/* -------------------------------- Vector --------------------------------- */

/**
 * @brief Strong vector type wrapping Array.
 *
 * @tparam T Element type (float or double).
 *
 * @note RT-SAFE: All operations noexcept, no allocations.
 */
template <typename T> class Vector {
public:
  using value_type = T;

  /* --------------------------- Construction ------------------------------ */

  /** @brief Construct from an existing Array view. */
  Vector(const Array<T>& v, VectorOrient orient) noexcept;

  /** @brief Construct from raw pointer + metadata. */
  Vector(T* data, std::size_t n, VectorOrient orient, Layout layout, std::size_t ld = 0) noexcept;

  /** @brief Construct from any ArrayBase view. */
  template <class D> Vector(const ArrayBase<T, D>& base, VectorOrient orient) noexcept;

  /* ----------------------------- Accessors ------------------------------- */

  std::size_t size() const noexcept;   ///< Number of elements.
  std::size_t length() const noexcept; ///< Alias for size().
  VectorOrient orient() const noexcept;
  Layout layout() const noexcept;
  std::size_t rows() const noexcept;
  std::size_t cols() const noexcept;
  std::size_t ld() const noexcept;
  T* data() noexcept;
  const T* data() const noexcept;

  /** @brief Underlying Array view (non-owning). */
  Array<T>& view() noexcept { return v_; }
  const Array<T>& view() const noexcept { return v_; }

  /* ------------------------- Vector Operations --------------------------- */

  /** @brief out = x . y (dot product). RT-SAFE. */
  uint8_t dotInto(const Vector<T>& y, T& out) const noexcept;

  /** @brief out = ||x||_2 (Euclidean norm). RT-SAFE. */
  uint8_t norm2Into(T& out) const noexcept;

  /** @brief x := x / ||x||_2 (in-place normalize). RT-SAFE. */
  uint8_t normalizeInPlace() noexcept;

  /** @brief y := alpha * x + y (AXPY-style). RT-SAFE. */
  uint8_t axpyInto(T alpha, Vector<T>& y) const noexcept;

  /** @brief z := x * y (Hadamard/elementwise product). RT-SAFE. */
  uint8_t hadamardInto(const Vector<T>& y, Vector<T>& z) const noexcept;

  /** @brief out := x + y (elementwise). RT-SAFE. */
  uint8_t addInto(const Vector<T>& y, Vector<T>& out) const noexcept;

  /** @brief out := x - y (elementwise). RT-SAFE. */
  uint8_t subInto(const Vector<T>& y, Vector<T>& out) const noexcept;

  /** @brief out := alpha * x (elementwise scale). RT-SAFE. */
  uint8_t scaleInto(T alpha, Vector<T>& out) const noexcept;

  /** @brief out := cross(x, y). Only for size == 3. RT-SAFE. */
  uint8_t crossInto(const Vector<T>& y, Vector<T>& out) const noexcept;

  /** @brief out := projection of y onto x. RT-SAFE. */
  uint8_t projectOntoInto(const Vector<T>& y, Vector<T>& out) const noexcept;

  /** @brief out := angle(x, y) in radians. RT-SAFE. */
  uint8_t angleBetweenInto(const Vector<T>& y, T& outRadians) const noexcept;

  /* ------------------------- Elementwise Operations ------------------------ */

  /** @brief out := |x| (elementwise absolute value). RT-SAFE. */
  uint8_t absInto(Vector<T>& out) const noexcept;

  /** @brief out := min(x, y) (elementwise minimum). RT-SAFE. */
  uint8_t minInto(const Vector<T>& y, Vector<T>& out) const noexcept;

  /** @brief out := max(x, y) (elementwise maximum). RT-SAFE. */
  uint8_t maxInto(const Vector<T>& y, Vector<T>& out) const noexcept;

  /* ----------------------------- Reductions -------------------------------- */

  /** @brief out := sum(x) (sum of all elements). RT-SAFE. */
  uint8_t sumInto(T& out) const noexcept;

  /** @brief out := mean(x) (average of all elements). RT-SAFE. */
  uint8_t meanInto(T& out) const noexcept;

private:
  Array<T> v_;
  VectorOrient orient_;

  uint8_t checkShape() const noexcept;
  uint8_t checkCompat(const Vector<T>& other) const noexcept;
  static bool isVectorShape(const Array<T>& v) noexcept;
};

} // namespace linalg
} // namespace math
} // namespace apex

#include "src/utilities/math/linalg/src/Vector.tpp"

#endif // APEX_MATH_LINALG_VECTOR_HPP
