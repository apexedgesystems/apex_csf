/**
 * @file ArrayBase.tpp
 * @brief Template implementation for ArrayBase.
 */
#ifndef APEX_MATH_LINALG_ARRAY_BASE_TPP
#define APEX_MATH_LINALG_ARRAY_BASE_TPP

#include "src/utilities/math/linalg/inc/ArrayBase.hpp"
#include "src/utilities/math/linalg/inc/ArrayStatus.hpp"

#include <utility>

namespace apex {
namespace math {
namespace linalg {

/* ----------------------------- Constructor ------------------------------- */

template <typename T, class Derived>
constexpr ArrayBase<T, Derived>::ArrayBase(T* data, std::size_t rows, std::size_t cols,
                                           Layout layout, std::size_t ld) noexcept
    : data_(data), rows_(rows), cols_(cols),
      ld_(ld ? ld : (layout == Layout::RowMajor ? cols : rows)), layout_(layout) {
  static_assert(std::is_trivial<T>::value || std::is_standard_layout<T>::value,
                "ArrayBase expects POD-like element types for BLAS/LAPACK/CUDA interop.");
}

/* ------------------------------ Accessors -------------------------------- */

template <typename T, class Derived> constexpr T* ArrayBase<T, Derived>::data() noexcept {
  return data_;
}

template <typename T, class Derived>
constexpr const T* ArrayBase<T, Derived>::data() const noexcept {
  return data_;
}

template <typename T, class Derived>
constexpr std::size_t ArrayBase<T, Derived>::rows() const noexcept {
  return rows_;
}

template <typename T, class Derived>
constexpr std::size_t ArrayBase<T, Derived>::cols() const noexcept {
  return cols_;
}

template <typename T, class Derived>
constexpr std::size_t ArrayBase<T, Derived>::ld() const noexcept {
  return ld_;
}

template <typename T, class Derived>
constexpr Layout ArrayBase<T, Derived>::layout() const noexcept {
  return layout_;
}

template <typename T, class Derived>
constexpr std::size_t ArrayBase<T, Derived>::size() const noexcept {
  return rows_ * cols_;
}

template <typename T, class Derived>
constexpr bool ArrayBase<T, Derived>::isContiguous() const noexcept {
  return (layout_ == Layout::RowMajor) ? (ld_ == cols_) : (ld_ == rows_);
}

/* ---------------------- Preconditioned Element Access -------------------- */

template <typename T, class Derived>
T& ArrayBase<T, Derived>::operator()(std::size_t r, std::size_t c) noexcept {
  return (layout_ == Layout::RowMajor) ? data_[r * ld_ + c] : data_[c * ld_ + r];
}

template <typename T, class Derived>
const T& ArrayBase<T, Derived>::operator()(std::size_t r, std::size_t c) const noexcept {
  return (layout_ == Layout::RowMajor) ? data_[r * ld_ + c] : data_[c * ld_ + r];
}

template <typename T, class Derived> T& ArrayBase<T, Derived>::operator[](std::size_t i) noexcept {
  if (layout_ == Layout::RowMajor) {
    const std::size_t R = i / cols_;
    const std::size_t C = i % cols_;
    return data_[R * ld_ + C];
  }
  const std::size_t C = i / rows_;
  const std::size_t R = i % rows_;
  return data_[C * ld_ + R];
}

template <typename T, class Derived>
const T& ArrayBase<T, Derived>::operator[](std::size_t i) const noexcept {
  if (layout_ == Layout::RowMajor) {
    const std::size_t R = i / cols_;
    const std::size_t C = i % cols_;
    return data_[R * ld_ + C];
  }
  const std::size_t C = i / rows_;
  const std::size_t R = i % rows_;
  return data_[C * ld_ + R];
}

/* ------------------------- Checked Accessors ----------------------------- */

template <typename T, class Derived>
uint8_t ArrayBase<T, Derived>::get(std::size_t r, std::size_t c, T& out) const noexcept {
  if (r >= rows_ || c >= cols_) {
    return static_cast<uint8_t>(Status::ERROR_OUT_OF_BOUNDS);
  }
  out = (layout_ == Layout::RowMajor) ? data_[r * ld_ + c] : data_[c * ld_ + r];
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T, class Derived>
uint8_t ArrayBase<T, Derived>::set(std::size_t r, std::size_t c, const T& v) noexcept {
  if (r >= rows_ || c >= cols_) {
    return static_cast<uint8_t>(Status::ERROR_OUT_OF_BOUNDS);
  }
  if (layout_ == Layout::RowMajor) {
    data_[r * ld_ + c] = v;
  } else {
    data_[c * ld_ + r] = v;
  }
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T, class Derived>
uint8_t ArrayBase<T, Derived>::get(std::size_t i, T& out) const noexcept {
  if (!isContiguous()) {
    return static_cast<uint8_t>(Status::ERROR_NON_CONTIGUOUS);
  }
  if (i >= size()) {
    return static_cast<uint8_t>(Status::ERROR_OUT_OF_BOUNDS);
  }
  if (layout_ == Layout::RowMajor) {
    const std::size_t R = i / cols_;
    const std::size_t C = i % cols_;
    out = data_[R * ld_ + C];
  } else {
    const std::size_t C = i / rows_;
    const std::size_t R = i % rows_;
    out = data_[C * ld_ + R];
  }
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T, class Derived>
uint8_t ArrayBase<T, Derived>::set(std::size_t i, const T& v) noexcept {
  if (!isContiguous()) {
    return static_cast<uint8_t>(Status::ERROR_NON_CONTIGUOUS);
  }
  if (i >= size()) {
    return static_cast<uint8_t>(Status::ERROR_OUT_OF_BOUNDS);
  }
  if (layout_ == Layout::RowMajor) {
    const std::size_t R = i / cols_;
    const std::size_t C = i % cols_;
    data_[R * ld_ + C] = v;
  } else {
    const std::size_t C = i / rows_;
    const std::size_t R = i % rows_;
    data_[C * ld_ + R] = v;
  }
  return static_cast<uint8_t>(Status::SUCCESS);
}

/* --------------------------- Layout Control ------------------------------ */

template <typename T, class Derived>
uint8_t ArrayBase<T, Derived>::setLayout(Layout newLayout) noexcept {
  if (newLayout == layout_) {
    return static_cast<uint8_t>(Status::SUCCESS);
  }
  if (!isContiguous()) {
    return static_cast<uint8_t>(Status::ERROR_NON_CONTIGUOUS);
  }
  std::swap(rows_, cols_);
  ld_ = (newLayout == Layout::RowMajor) ? cols_ : rows_;
  layout_ = newLayout;
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T, class Derived>
ArrayBase<T, Derived> ArrayBase<T, Derived>::transposeView() const noexcept {
  const Layout NEW_LAYOUT = (layout_ == Layout::RowMajor) ? Layout::ColMajor : Layout::RowMajor;
  return ArrayBase<T, Derived>{data_, cols_, rows_, NEW_LAYOUT, ld_};
}

/* ------------------------------ Utilities -------------------------------- */

template <typename T, class Derived> void ArrayBase<T, Derived>::swap(ArrayBase& other) noexcept {
  using std::swap;
  swap(data_, other.data_);
  swap(rows_, other.rows_);
  swap(cols_, other.cols_);
  swap(ld_, other.ld_);
  swap(layout_, other.layout_);
}

/* --------------------------- Free Functions ------------------------------ */

template <typename T, class D1, class D2>
bool compare(const ArrayBase<T, D1>& a, const ArrayBase<T, D2>& b) noexcept {
  if (a.rows() != b.rows() || a.cols() != b.cols()) {
    return false;
  }

  // Fast path for matching contiguous layouts.
  if (a.isContiguous() && b.isContiguous() && a.layout() == b.layout()) {
    for (std::size_t i = 0, n = a.size(); i < n; ++i) {
      if (a[i] != b[i]) {
        return false;
      }
    }
    return true;
  }

  // General path.
  for (std::size_t r = 0; r < a.rows(); ++r) {
    for (std::size_t c = 0; c < a.cols(); ++c) {
      T va{};
      T vb{};
      if (a.get(r, c, va) != static_cast<uint8_t>(Status::SUCCESS)) {
        return false;
      }
      if (b.get(r, c, vb) != static_cast<uint8_t>(Status::SUCCESS)) {
        return false;
      }
      if (va != vb) {
        return false;
      }
    }
  }
  return true;
}

} // namespace linalg
} // namespace math
} // namespace apex

#endif // APEX_MATH_LINALG_ARRAY_BASE_TPP
