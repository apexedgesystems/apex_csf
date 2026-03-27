/**
 * @file PbarTriangle.cpp
 * @brief CPU implementation of fully normalized Legendre triangle computation.
 */

#include "src/utilities/math/legendre/inc/PbarTriangle.hpp"

#include <cmath>

namespace apex {
namespace math {
namespace legendre {

/* ----------------------------- Recurrence Coefficients ----------------------------- */

bool computeRecurrenceCoefficients(int n, double* aOut, double* bOut, std::size_t outLen) noexcept {
  if (n < 0 || !aOut || !bOut)
    return false;

  const std::size_t NEED = pbarTriangleSize(n);
  if (outLen < NEED)
    return false;

  // Initialize all coefficients to 0
  for (std::size_t i = 0; i < NEED; ++i) {
    aOut[i] = 0.0;
    bOut[i] = 0.0;
  }

  // Compute A and B for n >= m+2 (where recurrence is used)
  for (int m = 0; m <= n; ++m) {
    for (int n2 = m + 2; n2 <= n; ++n2) {
      const double NM = static_cast<double>(n2 - m);
      const double NPM = static_cast<double>(n2 + m);
      const double A = std::sqrt(((2.0 * n2 + 1.0) * (2.0 * n2 - 1.0)) / (NM * NPM));
      const double B =
          std::sqrt(((2.0 * n2 + 1.0) * (NPM - 1.0) * (NM - 1.0)) / ((2.0 * n2 - 3.0) * NM * NPM));
      aOut[pbarTriangleIndex(n2, m)] = A;
      bOut[pbarTriangleIndex(n2, m)] = B;
    }
  }

  return true;
}

RecurrenceCoefficients computeRecurrenceCoefficientsVector(int n) noexcept {
  RecurrenceCoefficients result;
  const std::size_t NEED = pbarTriangleSize(n);
  result.A.resize(NEED);
  result.B.resize(NEED);
  computeRecurrenceCoefficients(n, result.A.data(), result.B.data(), NEED);
  return result;
}

bool computeRecurrenceCoefficientsInterleaved(int n, double* abOut, std::size_t outLen) noexcept {
  if (n < 0 || !abOut)
    return false;

  const std::size_t TRI = pbarTriangleSize(n);
  const std::size_t NEED = TRI * 2;
  if (outLen < NEED)
    return false;

  // Initialize all coefficients to 0
  for (std::size_t i = 0; i < NEED; ++i) {
    abOut[i] = 0.0;
  }

  // Compute A and B interleaved: AB[2*idx] = A, AB[2*idx+1] = B
  for (int m = 0; m <= n; ++m) {
    for (int n2 = m + 2; n2 <= n; ++n2) {
      const double NM = static_cast<double>(n2 - m);
      const double NPM = static_cast<double>(n2 + m);
      const double A = std::sqrt(((2.0 * n2 + 1.0) * (2.0 * n2 - 1.0)) / (NM * NPM));
      const double B =
          std::sqrt(((2.0 * n2 + 1.0) * (NPM - 1.0) * (NM - 1.0)) / ((2.0 * n2 - 3.0) * NM * NPM));
      const std::size_t idx = pbarTriangleIndex(n2, m);
      abOut[idx * 2] = A;
      abOut[idx * 2 + 1] = B;
    }
  }

  return true;
}

/* ----------------------------- API ----------------------------- */

bool computeNormalizedPbarTriangle(int n, double x, double* out, std::size_t outLen) noexcept {
  if (n < 0 || !out)
    return false;

  // Clamp x for numerical safety
  if (x > 1.0)
    x = 1.0;
  if (x < -1.0)
    x = -1.0;

  const std::size_t NEED = pbarTriangleSize(n);
  if (outLen < NEED)
    return false;

  // Handle N == 0 early
  out[0] = 1.0;
  if (n == 0)
    return true;

  // We evaluate with x = sin(phi); C = cos(phi) = sqrt(1 - x^2)
  const double X2 = x * x;
  const double C = (X2 < 1.0) ? std::sqrt(1.0 - X2) : 0.0;

  // Pbar_00
  out[pbarTriangleIndex(0, 0)] = 1.0;

  // m = 1 diagonal and n = 1, m = 0
  out[pbarTriangleIndex(1, 1)] = std::sqrt(3.0) * C * out[pbarTriangleIndex(0, 0)];
  out[pbarTriangleIndex(1, 0)] = std::sqrt(3.0) * x * out[pbarTriangleIndex(0, 0)];

  // Diagonal m = 2..N: Pbar_{m m} = sqrt((2m+1)/(2m)) * C * Pbar_{m-1,m-1}
  for (int m = 2; m <= n; ++m) {
    const double K = std::sqrt((2.0 * m + 1.0) / (2.0 * m));
    out[pbarTriangleIndex(m, m)] = K * C * out[pbarTriangleIndex(m - 1, m - 1)];
  }

  // First off-diagonal: Pbar_{m+1,m} = sqrt(2m+3) * x * Pbar_{m m}
  for (int m = 0; m <= n - 1; ++m) {
    const double K = std::sqrt(2.0 * m + 3.0);
    out[pbarTriangleIndex(m + 1, m)] = K * x * out[pbarTriangleIndex(m, m)];
  }

  // Upward recurrence for n >= m+2:
  //   Pbar_{n m} = A_{n m} * x * Pbar_{n-1,m} - B_{n m} * Pbar_{n-2,m}
  // with fully-normalized coefficients (Holmes-Featherstone form):
  //   A_{n m} = sqrt(((2n+1)(2n-1))/((n-m)(n+m)))
  //   B_{n m} = sqrt(((2n+1)(n+m-1)(n-m-1))/((2n-3)(n-m)(n+m)))
  for (int m = 0; m <= n; ++m) {
    for (int n2 = m + 2; n2 <= n; ++n2) {
      const double NM = static_cast<double>(n2 - m);
      const double NPM = static_cast<double>(n2 + m);
      const double A = std::sqrt(((2.0 * n2 + 1.0) * (2.0 * n2 - 1.0)) / (NM * NPM));
      const double B =
          std::sqrt(((2.0 * n2 + 1.0) * (NPM - 1.0) * (NM - 1.0)) / ((2.0 * n2 - 3.0) * NM * NPM));
      out[pbarTriangleIndex(n2, m)] =
          A * x * out[pbarTriangleIndex(n2 - 1, m)] - B * out[pbarTriangleIndex(n2 - 2, m)];
    }
  }

  return true;
}

std::vector<double> computeNormalizedPbarTriangleVector(int n, double x) noexcept {
  std::vector<double> tri;
  const std::size_t NEED = pbarTriangleSize(n);
  tri.resize(NEED);
  computeNormalizedPbarTriangle(n, x, tri.data(), tri.size());
  return tri;
}

bool computeNormalizedPbarTriangleCached(int n, double x, const double* A, const double* B,
                                         double* out, std::size_t outLen) noexcept {
  if (n < 0 || !out || !A || !B)
    return false;

  // Clamp x for numerical safety
  if (x > 1.0)
    x = 1.0;
  if (x < -1.0)
    x = -1.0;

  const std::size_t NEED = pbarTriangleSize(n);
  if (outLen < NEED)
    return false;

  // Handle N == 0 early
  out[0] = 1.0;
  if (n == 0)
    return true;

  // We evaluate with x = sin(phi); C = cos(phi) = sqrt(1 - x^2)
  const double X2 = x * x;
  const double C = (X2 < 1.0) ? std::sqrt(1.0 - X2) : 0.0;

  // Precompute sqrt(3) once
  static const double SQRT3 = std::sqrt(3.0);

  // Pbar_00 = 1.0 (already set above)
  // Pbar_10 at index 1, Pbar_11 at index 2
  out[1] = SQRT3 * x; // Pbar_10
  out[2] = SQRT3 * C; // Pbar_11

  // Diagonal m = 2..N: Pbar_{m,m} at index m*(m+1)/2 + m = m*(m+3)/2
  // Each diagonal element depends on previous diagonal: idx_{m-1,m-1} = (m-1)*m/2 + (m-1)
  std::size_t diagIdx = 2;     // Start at Pbar_11
  std::size_t prevDiagIdx = 0; // Pbar_00
  for (int m = 2; m <= n; ++m) {
    prevDiagIdx = diagIdx;
    diagIdx = static_cast<std::size_t>(m) * static_cast<std::size_t>(m + 1) / 2 +
              static_cast<std::size_t>(m);
    const double K = std::sqrt((2.0 * m + 1.0) / (2.0 * m));
    out[diagIdx] = K * C * out[prevDiagIdx];
  }

  // First off-diagonal: Pbar_{m+1,m} = sqrt(2m+3) * x * Pbar_{m,m}
  // Index of (m+1, m) = (m+1)*(m+2)/2 + m
  // Index of (m, m) = m*(m+1)/2 + m
  for (int m = 0; m <= n - 1; ++m) {
    const std::size_t diagMIdx = static_cast<std::size_t>(m) * static_cast<std::size_t>(m + 1) / 2 +
                                 static_cast<std::size_t>(m);
    const std::size_t offDiagIdx =
        static_cast<std::size_t>(m + 1) * static_cast<std::size_t>(m + 2) / 2 +
        static_cast<std::size_t>(m);
    const double K = std::sqrt(2.0 * m + 3.0);
    out[offDiagIdx] = K * x * out[diagMIdx];
  }

  // Upward recurrence using PRECOMPUTED A/B coefficients
  // Process column by column (fixed m, varying n2)
  // For column m: indices are m*(m+1)/2+m, (m+1)*(m+2)/2+m, (m+2)*(m+3)/2+m, ...
  // The step between consecutive n values in same column: idx(n,m) - idx(n-1,m) = n
  const double* __restrict__ pA = A;
  const double* __restrict__ pB = B;
  double* __restrict__ pOut = out;

  for (int m = 0; m <= n; ++m) {
    // Base index for (m+2, m)
    std::size_t idx = static_cast<std::size_t>(m + 2) * static_cast<std::size_t>(m + 3) / 2 +
                      static_cast<std::size_t>(m);
    std::size_t idxN1 = static_cast<std::size_t>(m + 1) * static_cast<std::size_t>(m + 2) / 2 +
                        static_cast<std::size_t>(m); // (m+1, m)
    std::size_t idxN2 = static_cast<std::size_t>(m) * static_cast<std::size_t>(m + 1) / 2 +
                        static_cast<std::size_t>(m); // (m, m)

    for (int n2 = m + 2; n2 <= n; ++n2) {
      pOut[idx] = pA[idx] * x * pOut[idxN1] - pB[idx] * pOut[idxN2];
      // Move to next row in same column
      idxN2 = idxN1;
      idxN1 = idx;
      idx += static_cast<std::size_t>(n2 + 1); // Step to next row
    }
  }

  return true;
}

bool computeNormalizedPbarTriangleCachedInterleaved(int n, double x, const double* AB, double* out,
                                                    std::size_t outLen) noexcept {
  if (n < 0 || !out || !AB)
    return false;

  // Clamp x for numerical safety
  if (x > 1.0)
    x = 1.0;
  if (x < -1.0)
    x = -1.0;

  const std::size_t NEED = pbarTriangleSize(n);
  if (outLen < NEED)
    return false;

  // Handle N == 0 early
  out[0] = 1.0;
  if (n == 0)
    return true;

  // We evaluate with x = sin(phi); C = cos(phi) = sqrt(1 - x^2)
  const double X2 = x * x;
  const double C = (X2 < 1.0) ? std::sqrt(1.0 - X2) : 0.0;

  // Precompute sqrt(3) once
  static const double SQRT3 = std::sqrt(3.0);

  // Pbar_00 = 1.0 (already set above)
  // Pbar_10 at index 1, Pbar_11 at index 2
  out[1] = SQRT3 * x; // Pbar_10
  out[2] = SQRT3 * C; // Pbar_11

  // Diagonal m = 2..N: Pbar_{m,m} at index m*(m+1)/2 + m = m*(m+3)/2
  std::size_t diagIdx = 2;     // Start at Pbar_11
  std::size_t prevDiagIdx = 0; // Pbar_00
  for (int m = 2; m <= n; ++m) {
    prevDiagIdx = diagIdx;
    diagIdx = static_cast<std::size_t>(m) * static_cast<std::size_t>(m + 1) / 2 +
              static_cast<std::size_t>(m);
    const double K = std::sqrt((2.0 * m + 1.0) / (2.0 * m));
    out[diagIdx] = K * C * out[prevDiagIdx];
  }

  // First off-diagonal: Pbar_{m+1,m} = sqrt(2m+3) * x * Pbar_{m,m}
  for (int m = 0; m <= n - 1; ++m) {
    const std::size_t diagMIdx = static_cast<std::size_t>(m) * static_cast<std::size_t>(m + 1) / 2 +
                                 static_cast<std::size_t>(m);
    const std::size_t offDiagIdx =
        static_cast<std::size_t>(m + 1) * static_cast<std::size_t>(m + 2) / 2 +
        static_cast<std::size_t>(m);
    const double K = std::sqrt(2.0 * m + 3.0);
    out[offDiagIdx] = K * x * out[diagMIdx];
  }

  // Upward recurrence using INTERLEAVED A/B coefficients
  // AB[2*idx] = A, AB[2*idx+1] = B
  const double* __restrict__ pAB = AB;
  double* __restrict__ pOut = out;

  for (int m = 0; m <= n; ++m) {
    // Base index for (m+2, m)
    std::size_t idx = static_cast<std::size_t>(m + 2) * static_cast<std::size_t>(m + 3) / 2 +
                      static_cast<std::size_t>(m);
    std::size_t idxN1 = static_cast<std::size_t>(m + 1) * static_cast<std::size_t>(m + 2) / 2 +
                        static_cast<std::size_t>(m); // (m+1, m)
    std::size_t idxN2 = static_cast<std::size_t>(m) * static_cast<std::size_t>(m + 1) / 2 +
                        static_cast<std::size_t>(m); // (m, m)

    for (int n2 = m + 2; n2 <= n; ++n2) {
      const std::size_t idx2 = idx * 2;
      pOut[idx] = pAB[idx2] * x * pOut[idxN1] - pAB[idx2 + 1] * pOut[idxN2];
      // Move to next row in same column
      idxN2 = idxN1;
      idxN1 = idx;
      idx += static_cast<std::size_t>(n2 + 1); // Step to next row
    }
  }

  return true;
}

} // namespace legendre
} // namespace math
} // namespace apex
