/**
 * @file Array.cpp
 * @brief Accelerated operations for Array (float/double).
 *
 * Uses BLAS/LAPACK when available and beneficial; falls back to naive
 * implementations for small matrices or when libraries are unavailable.
 */

#include "src/utilities/math/linalg/inc/Array.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <vector>

namespace apex {
namespace math {
namespace linalg {

using compat::blas::lapackInfoToUint8;
using compat::blas::tightLd;
using compat::blas::toCblasLayout;
using BlasLayout = compat::blas::Layout;

namespace detail {

/* ------------------------- BLAS/LAPACK Traits ---------------------------- */

template <typename T> struct BlasLapackTraits;

template <> struct BlasLapackTraits<float> {
#if COMPAT_HAVE_CBLAS
  static inline void gemm(int order, int transA, int transB, int m, int n, int k, float alpha,
                          const float* A, int lda, const float* B, int ldb, float beta, float* C,
                          int ldc) noexcept {
    cblas_sgemm(static_cast<CBLAS_ORDER>(order), static_cast<CBLAS_TRANSPOSE>(transA),
                static_cast<CBLAS_TRANSPOSE>(transB), m, n, k, alpha, A, lda, B, ldb, beta, C, ldc);
  }
#endif
#if COMPAT_HAVE_LAPACKE
  static inline int getrfRowMajor(int n, float* A, int lda, int* ipiv) noexcept {
    return LAPACKE_sgetrf(LAPACK_ROW_MAJOR, n, n, A, lda, ipiv);
  }
  static inline int getriRowMajor(int n, float* A, int lda, const int* ipiv) noexcept {
    return LAPACKE_sgetri(LAPACK_ROW_MAJOR, n, A, lda, const_cast<int*>(ipiv));
  }
#endif
};

template <> struct BlasLapackTraits<double> {
#if COMPAT_HAVE_CBLAS
  static inline void gemm(int order, int transA, int transB, int m, int n, int k, double alpha,
                          const double* A, int lda, const double* B, int ldb, double beta,
                          double* C, int ldc) noexcept {
    cblas_dgemm(static_cast<CBLAS_ORDER>(order), static_cast<CBLAS_TRANSPOSE>(transA),
                static_cast<CBLAS_TRANSPOSE>(transB), m, n, k, alpha, A, lda, B, ldb, beta, C, ldc);
  }
#endif
#if COMPAT_HAVE_LAPACKE
  static inline int getrfRowMajor(int n, double* A, int lda, int* ipiv) noexcept {
    return LAPACKE_dgetrf(LAPACK_ROW_MAJOR, n, n, A, lda, ipiv);
  }
  static inline int getriRowMajor(int n, double* A, int lda, const int* ipiv) noexcept {
    return LAPACKE_dgetri(LAPACK_ROW_MAJOR, n, A, lda, const_cast<int*>(ipiv));
  }
#endif
};

/* ----------------------------- Helpers ----------------------------------- */

inline bool isSquare(std::size_t r, std::size_t c) noexcept { return r == c; }

inline bool fitsInt(std::size_t x) noexcept {
  constexpr std::size_t K_I_MAX = static_cast<std::size_t>(std::numeric_limits<int>::max());
  return x <= K_I_MAX;
}

inline int toInt(std::size_t x) noexcept { return static_cast<int>(x); }

#if COMPAT_HAVE_CBLAS
inline int toCblasTranspose(Transpose t) noexcept {
  return (t == Transpose::NoTrans) ? CblasNoTrans : CblasTrans;
}
#endif

/// Threshold below which we use naive GEMM instead of BLAS.
/// For small matrices, function call overhead makes BLAS slower.
constexpr std::size_t K_BLAS_THRESHOLD = 16;

/// Check if matrix is small enough for naive implementation.
inline bool useNaiveGemm(std::size_t m, std::size_t n, std::size_t k) noexcept {
  return (m <= K_BLAS_THRESHOLD && n <= K_BLAS_THRESHOLD && k <= K_BLAS_THRESHOLD);
}

/* ------------------------- Naive Implementations ------------------------- */

/**
 * @brief Naive GEMM: C = alpha * A * B + beta * C.
 *
 * Works for any layout by using operator() indexing.
 */
template <typename T>
inline void naiveGemm(const Array<T>& A, const Array<T>& B, Array<T>& C, T alpha, T beta,
                      std::size_t m, std::size_t n, std::size_t k) noexcept {
  for (std::size_t i = 0; i < m; ++i) {
    for (std::size_t j = 0; j < n; ++j) {
      T sum = T(0);
      for (std::size_t p = 0; p < k; ++p) {
        sum += A(i, p) * B(p, j);
      }
      C(i, j) = alpha * sum + beta * C(i, j);
    }
  }
}

/**
 * @brief Naive GEMM with transpose flags.
 */
template <typename T>
inline void naiveGemmTrans(const Array<T>& A, Transpose transA, const Array<T>& B, Transpose transB,
                           Array<T>& C, T alpha, T beta, std::size_t m, std::size_t n,
                           std::size_t k) noexcept {
  for (std::size_t i = 0; i < m; ++i) {
    for (std::size_t j = 0; j < n; ++j) {
      T sum = T(0);
      for (std::size_t p = 0; p < k; ++p) {
        const T A_VAL = (transA == Transpose::NoTrans) ? A(i, p) : A(p, i);
        const T B_VAL = (transB == Transpose::NoTrans) ? B(p, j) : B(j, p);
        sum += A_VAL * B_VAL;
      }
      C(i, j) = alpha * sum + beta * C(i, j);
    }
  }
}

/**
 * @brief Naive 2x2 determinant.
 */
template <typename T> inline T det2x2(const Array<T>& A) noexcept {
  return A(0, 0) * A(1, 1) - A(0, 1) * A(1, 0);
}

/**
 * @brief Naive 3x3 determinant.
 */
template <typename T> inline T det3x3(const Array<T>& A) noexcept {
  return A(0, 0) * (A(1, 1) * A(2, 2) - A(1, 2) * A(2, 1)) -
         A(0, 1) * (A(1, 0) * A(2, 2) - A(1, 2) * A(2, 0)) +
         A(0, 2) * (A(1, 0) * A(2, 1) - A(1, 1) * A(2, 0));
}

/**
 * @brief Naive 2x2 inverse in-place.
 */
template <typename T> inline uint8_t inverse2x2InPlace(Array<T>& A) noexcept {
  const T DET = det2x2(A);
  if (std::abs(DET) < std::numeric_limits<T>::epsilon()) {
    return static_cast<uint8_t>(Status::ERROR_SINGULAR);
  }

  const T INV_DET = T(1) / DET;
  const T A00 = A(0, 0);
  const T A01 = A(0, 1);
  const T A10 = A(1, 0);
  const T A11 = A(1, 1);

  A(0, 0) = A11 * INV_DET;
  A(0, 1) = -A01 * INV_DET;
  A(1, 0) = -A10 * INV_DET;
  A(1, 1) = A00 * INV_DET;

  return static_cast<uint8_t>(Status::SUCCESS);
}

/**
 * @brief Naive 3x3 inverse in-place.
 */
template <typename T> inline uint8_t inverse3x3InPlace(Array<T>& A) noexcept {
  const T DET = det3x3(A);
  if (std::abs(DET) < std::numeric_limits<T>::epsilon()) {
    return static_cast<uint8_t>(Status::ERROR_SINGULAR);
  }

  const T INV_DET = T(1) / DET;

  // Compute cofactor matrix
  const T A00 = A(0, 0), A01 = A(0, 1), A02 = A(0, 2);
  const T A10 = A(1, 0), A11 = A(1, 1), A12 = A(1, 2);
  const T A20 = A(2, 0), A21 = A(2, 1), A22 = A(2, 2);

  // Cofactors (transposed for adjugate)
  A(0, 0) = (A11 * A22 - A12 * A21) * INV_DET;
  A(0, 1) = (A02 * A21 - A01 * A22) * INV_DET;
  A(0, 2) = (A01 * A12 - A02 * A11) * INV_DET;
  A(1, 0) = (A12 * A20 - A10 * A22) * INV_DET;
  A(1, 1) = (A00 * A22 - A02 * A20) * INV_DET;
  A(1, 2) = (A02 * A10 - A00 * A12) * INV_DET;
  A(2, 0) = (A10 * A21 - A11 * A20) * INV_DET;
  A(2, 1) = (A01 * A20 - A00 * A21) * INV_DET;
  A(2, 2) = (A00 * A11 - A01 * A10) * INV_DET;

  return static_cast<uint8_t>(Status::SUCCESS);
}

} // namespace detail

/* --------------------------------- GEMM ---------------------------------- */

template <typename T>
uint8_t Array<T>::gemmInto(const Array& B, Array& C, T alpha, T beta) const noexcept {
  return this->gemmIntoTrans(Transpose::NoTrans, B, Transpose::NoTrans, C, alpha, beta);
}

template <typename T>
uint8_t Array<T>::gemmIntoTrans(Transpose transA, const Array& B, Transpose transB, Array& C,
                                T alpha, T beta) const noexcept {
  const int ORDER_A = toCblasLayout(static_cast<BlasLayout>(this->layout()));
  const int ORDER_B = toCblasLayout(static_cast<BlasLayout>(B.layout()));
  const int ORDER_C = toCblasLayout(static_cast<BlasLayout>(C.layout()));

  if (ORDER_A < 0 || ORDER_B < 0 || ORDER_C < 0) {
    return static_cast<uint8_t>(Status::ERROR_INVALID_LAYOUT);
  }
  if (!(ORDER_A == ORDER_B && ORDER_A == ORDER_C)) {
    return static_cast<uint8_t>(Status::ERROR_INVALID_LAYOUT);
  }

  const std::size_t A_ROWS = (transA == Transpose::NoTrans) ? this->rows() : this->cols();
  const std::size_t A_COLS = (transA == Transpose::NoTrans) ? this->cols() : this->rows();
  const std::size_t B_ROWS = (transB == Transpose::NoTrans) ? B.rows() : B.cols();
  const std::size_t B_COLS = (transB == Transpose::NoTrans) ? B.cols() : B.rows();

  if (A_COLS != B_ROWS || C.rows() != A_ROWS || C.cols() != B_COLS) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  if (!this->data() || !B.data() || !C.data()) {
    return static_cast<uint8_t>(Status::ERROR_UNKNOWN);
  }

  const std::size_t M = A_ROWS;
  const std::size_t N = B_COLS;
  const std::size_t K = A_COLS;

  // Use naive implementation for small matrices or when BLAS unavailable
#if COMPAT_HAVE_CBLAS
  if (!detail::useNaiveGemm(M, N, K)) {
    if (!detail::fitsInt(M) || !detail::fitsInt(N) || !detail::fitsInt(K) ||
        !detail::fitsInt(this->ld()) || !detail::fitsInt(B.ld()) || !detail::fitsInt(C.ld())) {
      return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
    }

    const int M_I = detail::toInt(M);
    const int N_I = detail::toInt(N);
    const int K_I = detail::toInt(K);
    const int LDA = detail::toInt(this->ld());
    const int LDB = detail::toInt(B.ld());
    const int LDC = detail::toInt(C.ld());
    const int TA = detail::toCblasTranspose(transA);
    const int TB = detail::toCblasTranspose(transB);

    detail::BlasLapackTraits<T>::gemm(ORDER_A, TA, TB, M_I, N_I, K_I, alpha, this->data(), LDA,
                                      B.data(), LDB, beta, C.data(), LDC);
    return static_cast<uint8_t>(Status::SUCCESS);
  }
#endif

  // Naive fallback
  detail::naiveGemmTrans(*this, transA, B, transB, C, alpha, beta, M, N, K);
  return static_cast<uint8_t>(Status::SUCCESS);
}

/* ------------------------------- Transpose ------------------------------- */

template <typename T> uint8_t Array<T>::transposeInto(Array& dst) const noexcept {
  if (dst.rows() != this->cols() || dst.cols() != this->rows()) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  if (!this->data() || !dst.data()) {
    return static_cast<uint8_t>(Status::ERROR_UNKNOWN);
  }

  const bool SRC_TIGHT =
      (this->ld() == tightLd(this->rows(), this->cols(), static_cast<BlasLayout>(this->layout())));
  const bool DST_TIGHT =
      (dst.ld() == tightLd(dst.rows(), dst.cols(), static_cast<BlasLayout>(dst.layout())));

  if (!SRC_TIGHT || !DST_TIGHT) {
    return static_cast<uint8_t>(Status::ERROR_NON_CONTIGUOUS);
  }

  const std::size_t M = this->rows();
  const std::size_t N = this->cols();

  for (std::size_t r = 0; r < M; ++r) {
    for (std::size_t c = 0; c < N; ++c) {
      dst(c, r) = (*this)(r, c);
    }
  }

  return static_cast<uint8_t>(Status::SUCCESS);
}

/* -------------------------------- Inverse -------------------------------- */

template <typename T> uint8_t Array<T>::inverseInPlace() noexcept {
  if (!detail::isSquare(this->rows(), this->cols())) {
    return static_cast<uint8_t>(Status::ERROR_NOT_SQUARE);
  }
  if (!this->data()) {
    return static_cast<uint8_t>(Status::ERROR_UNKNOWN);
  }

  const std::size_t N = this->rows();

  // Use naive for small matrices
  if (N == 1) {
    if (std::abs((*this)(0, 0)) < std::numeric_limits<T>::epsilon()) {
      return static_cast<uint8_t>(Status::ERROR_SINGULAR);
    }
    (*this)(0, 0) = T(1) / (*this)(0, 0);
    return static_cast<uint8_t>(Status::SUCCESS);
  }

  if (N == 2) {
    return detail::inverse2x2InPlace(*this);
  }

  if (N == 3) {
    return detail::inverse3x3InPlace(*this);
  }

  // For larger matrices, use LAPACKE
#if COMPAT_HAVE_LAPACKE
  using Traits = detail::BlasLapackTraits<T>;

  if (this->layout() != Layout::RowMajor) {
    return static_cast<uint8_t>(Status::ERROR_INVALID_LAYOUT);
  }

  const std::size_t TIGHT =
      tightLd(this->rows(), this->cols(), static_cast<BlasLayout>(this->layout()));
  if (this->ld() != TIGHT) {
    return static_cast<uint8_t>(Status::ERROR_NON_CONTIGUOUS);
  }

  if (!detail::fitsInt(N)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  const int N_INT = detail::toInt(N);
  std::vector<int> ipiv(static_cast<std::size_t>(N_INT));

  int info = Traits::getrfRowMajor(N_INT, this->data(), N_INT, ipiv.data());
  if (info != 0) {
    return lapackInfoToUint8(info);
  }

  info = Traits::getriRowMajor(N_INT, this->data(), N_INT, ipiv.data());
  if (info != 0) {
    return lapackInfoToUint8(info);
  }

  return static_cast<uint8_t>(Status::SUCCESS);
#else
  return static_cast<uint8_t>(Status::ERROR_UNSUPPORTED_OP);
#endif
}

template <typename T> uint8_t Array<T>::inverseInto(Array& dst) const noexcept {
  if (!detail::isSquare(this->rows(), this->cols())) {
    return static_cast<uint8_t>(Status::ERROR_NOT_SQUARE);
  }

  if (dst.rows() != this->rows() || dst.cols() != this->cols()) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  if (!this->data() || !dst.data()) {
    return static_cast<uint8_t>(Status::ERROR_UNKNOWN);
  }

  const std::size_t N = this->rows();

  // Copy to dst, then invert in place
  for (std::size_t r = 0; r < N; ++r) {
    for (std::size_t c = 0; c < N; ++c) {
      dst(r, c) = (*this)(r, c);
    }
  }

  return dst.inverseInPlace();
}

/* ------------------------------ Determinant ------------------------------ */

template <typename T> uint8_t Array<T>::determinant(T& out) const noexcept {
  out = T(0);

  if (!detail::isSquare(this->rows(), this->cols())) {
    return static_cast<uint8_t>(Status::ERROR_NOT_SQUARE);
  }
  if (!this->data()) {
    return static_cast<uint8_t>(Status::ERROR_UNKNOWN);
  }

  const std::size_t N = this->rows();

  // Naive for small matrices
  if (N == 0) {
    out = T(1);
    return static_cast<uint8_t>(Status::SUCCESS);
  }

  if (N == 1) {
    out = (*this)(0, 0);
    return static_cast<uint8_t>(Status::SUCCESS);
  }

  if (N == 2) {
    out = detail::det2x2(*this);
    return static_cast<uint8_t>(Status::SUCCESS);
  }

  if (N == 3) {
    out = detail::det3x3(*this);
    return static_cast<uint8_t>(Status::SUCCESS);
  }

  // For larger matrices, use LAPACKE
#if COMPAT_HAVE_LAPACKE
  using Traits = detail::BlasLapackTraits<T>;

  if (!detail::fitsInt(N)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  const std::size_t NN = N * N;
  std::vector<T> tmp(NN);

  for (std::size_t r = 0; r < N; ++r) {
    for (std::size_t c = 0; c < N; ++c) {
      tmp[r * N + c] = (*this)(r, c);
    }
  }

  const int N_INT = detail::toInt(N);
  std::vector<int> ipiv(static_cast<std::size_t>(N_INT));

  int info = Traits::getrfRowMajor(N_INT, tmp.data(), N_INT, ipiv.data());
  if (info != 0) {
    return lapackInfoToUint8(info);
  }

  T det = T(1);
  int swapParity = 0;
  for (int i = 0; i < N_INT; ++i) {
    det *= tmp[static_cast<std::size_t>(i) * N + static_cast<std::size_t>(i)];
    if (ipiv[static_cast<std::size_t>(i)] != (i + 1)) {
      ++swapParity;
    }
  }
  if (swapParity % 2) {
    det = -det;
  }

  out = det;
  return static_cast<uint8_t>(Status::SUCCESS);
#else
  return static_cast<uint8_t>(Status::ERROR_UNSUPPORTED_OP);
#endif
}

/* --------------------------------- Trace --------------------------------- */

template <typename T> uint8_t Array<T>::trace(T& out) const noexcept {
  out = T(0);

  if (!detail::isSquare(this->rows(), this->cols())) {
    return static_cast<uint8_t>(Status::ERROR_NOT_SQUARE);
  }

  const std::size_t N = this->rows();
  T t = T(0);
  for (std::size_t i = 0; i < N; ++i) {
    t += (*this)(i, i);
  }

  out = t;
  return static_cast<uint8_t>(Status::SUCCESS);
}

/* ------------------------- Explicit Instantiation ------------------------ */

template class Array<float>;
template class Array<double>;

} // namespace linalg
} // namespace math
} // namespace apex
