#ifndef APEX_MATH_INTEGRATION_STATEVECTOR_HPP
#define APEX_MATH_INTEGRATION_STATEVECTOR_HPP
/**
 * @file StateVector.hpp
 * @brief Fixed-size state vector for RT-safe ODE integration.
 *
 * Design goals:
 *  - Stack-allocated, trivially copyable
 *  - Arithmetic operations for integrator compatibility
 *  - Zero heap allocation
 */

#include <array>
#include <cmath>
#include <cstddef>

namespace apex {
namespace math {
namespace integration {

/* ------------------------------ StateVector ------------------------------- */

/**
 * @brief Fixed-size state vector for ODE integration.
 *
 * Provides arithmetic operations required by integrators (+, -, * scalar).
 * Stack-allocated, trivially copyable, zero-allocation.
 *
 * @tparam N Number of state elements.
 *
 * @note RT-safe: All operations are inline, no allocations.
 */
template <std::size_t N> struct StateVector {
  std::array<double, N> x{};

  /* -------------------------- Construction ------------------------------ */

  /** @brief Default constructor (zero-initialized). */
  constexpr StateVector() noexcept = default;

  /** @brief Construct from initializer list. */
  constexpr StateVector(std::initializer_list<double> init) noexcept {
    std::size_t i = 0;
    for (auto v : init) {
      if (i >= N)
        break;
      x[i++] = v;
    }
  }

  /** @brief Construct from std::array. */
  constexpr explicit StateVector(const std::array<double, N>& arr) noexcept : x(arr) {}

  /* -------------------------- Element Access ---------------------------- */

  /** @brief Element access (mutable). */
  constexpr double& operator[](std::size_t i) noexcept { return x[i]; }

  /** @brief Element access (const). */
  constexpr const double& operator[](std::size_t i) const noexcept { return x[i]; }

  /** @brief Number of elements. */
  static constexpr std::size_t size() noexcept { return N; }

  /** @brief Pointer to underlying data. */
  constexpr double* data() noexcept { return x.data(); }

  /** @brief Const pointer to underlying data. */
  constexpr const double* data() const noexcept { return x.data(); }

  /* -------------------------- Arithmetic -------------------------------- */

  /** @brief Vector addition. */
  constexpr StateVector operator+(const StateVector& rhs) const noexcept {
    StateVector result;
    for (std::size_t i = 0; i < N; ++i) {
      result.x[i] = x[i] + rhs.x[i];
    }
    return result;
  }

  /** @brief Vector subtraction. */
  constexpr StateVector operator-(const StateVector& rhs) const noexcept {
    StateVector result;
    for (std::size_t i = 0; i < N; ++i) {
      result.x[i] = x[i] - rhs.x[i];
    }
    return result;
  }

  /** @brief Scalar multiplication (right). */
  constexpr StateVector operator*(double scalar) const noexcept {
    StateVector result;
    for (std::size_t i = 0; i < N; ++i) {
      result.x[i] = x[i] * scalar;
    }
    return result;
  }

  /** @brief In-place addition. */
  constexpr StateVector& operator+=(const StateVector& rhs) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
      x[i] += rhs.x[i];
    }
    return *this;
  }

  /** @brief In-place subtraction. */
  constexpr StateVector& operator-=(const StateVector& rhs) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
      x[i] -= rhs.x[i];
    }
    return *this;
  }

  /** @brief In-place scalar multiplication. */
  constexpr StateVector& operator*=(double scalar) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
      x[i] *= scalar;
    }
    return *this;
  }

  /* -------------------------- Norms ------------------------------------- */

  /** @brief Euclidean norm (L2). */
  double norm() const noexcept {
    double sum = 0.0;
    for (std::size_t i = 0; i < N; ++i) {
      sum += x[i] * x[i];
    }
    return std::sqrt(sum);
  }

  /** @brief Infinity norm (max absolute value). */
  double normInf() const noexcept {
    double maxVal = 0.0;
    for (std::size_t i = 0; i < N; ++i) {
      double absVal = std::fabs(x[i]);
      if (absVal > maxVal)
        maxVal = absVal;
    }
    return maxVal;
  }

  /** @brief Dot product with another vector. */
  constexpr double dot(const StateVector& rhs) const noexcept {
    double sum = 0.0;
    for (std::size_t i = 0; i < N; ++i) {
      sum += x[i] * rhs.x[i];
    }
    return sum;
  }
};

/* -------------------------- Free Functions ------------------------------ */

/** @brief Scalar multiplication (left). */
template <std::size_t N>
constexpr StateVector<N> operator*(double scalar, const StateVector<N>& v) noexcept {
  return v * scalar;
}

/* -------------------------- Common Type Aliases ------------------------- */

/** @brief 3-element state (position OR velocity). */
using State3 = StateVector<3>;

/** @brief 6-element state (position + velocity). */
using State6 = StateVector<6>;

/** @brief 9-element state (position + velocity + acceleration). */
using State9 = StateVector<9>;

/** @brief 12-element state (6DOF: pos, vel, euler angles, angular vel). */
using State12 = StateVector<12>;

/** @brief 13-element state (6DOF with quaternion orientation). */
using State13 = StateVector<13>;

} // namespace integration
} // namespace math
} // namespace apex

#endif // APEX_MATH_INTEGRATION_STATEVECTOR_HPP
