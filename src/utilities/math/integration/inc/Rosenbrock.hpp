#ifndef APEX_MATH_INTEGRATION_ROSENBROCK_HPP
#define APEX_MATH_INTEGRATION_ROSENBROCK_HPP
/**
 * @file Rosenbrock.hpp
 * @brief Rosenbrock (linearly implicit) methods for moderately stiff systems.
 *
 * Only requires one Jacobian factorization per step (vs Newton iteration).
 * Good balance between stability and computational cost.
 *
 * Methods:
 *  - ROS2: Second-order, L-stable
 *  - ROS3: Third-order, L-stable
 *
 * Use cases: Chemical kinetics, battery management, thermal systems.
 * @note RT-safe with RT-safe callbacks.
 */

#include "src/utilities/math/integration/inc/StepSize.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace apex {
namespace math {
namespace integration {

/* -------------------------------- Status ---------------------------------- */

enum class RosenbrockStatus : uint8_t {
  SUCCESS = 0,
  ERROR_INVALID_STEP = 1 ///< Step size is not positive and finite.
};

/* ------------------------------ ROS2 Options ------------------------------ */

/**
 * @brief Options for Rosenbrock methods.
 *
 * @tparam State State type.
 */
template <typename State> struct RosenbrockOptions {
  /**
   * @brief Compute (I - gamma*dt*J) or its factorization.
   *
   * For efficiency, return a factored form that linearSolve can use.
   * @param x Current state.
   * @param t Current time.
   * @param gammaDt gamma * dt factor.
   * @return Matrix/factorization for linear solve.
   */
  std::function<State(const State& x, double t, double gammaDt)> computeMatrix;

  /**
   * @brief Solve (I - gamma*dt*J) * k = rhs for k.
   * @param matrix Factored matrix from computeMatrix.
   * @param rhs Right-hand side.
   * @return Solution k.
   */
  std::function<State(const State& matrix, const State& rhs)> linearSolve;

  /**
   * @brief Partial derivative of f with respect to time, df/dt at (x, t).
   *
   * Leave empty when f does not depend on time. A Rosenbrock method uses
   * df/dt as it uses the Jacobian. For a time-dependent f stepped without
   * it, ROS3 falls to about first order, and ROS2 falls to first order on
   * stiff problems.
   * @param x Current state.
   * @param t Current time.
   * @return df/dt with the state held fixed.
   */
  std::function<State(const State& x, double t)> timeDerivative;
};

/* ---------------------------------- ROS2 ---------------------------------- */

/**
 * @brief ROS2: Second-order L-stable Rosenbrock method.
 *
 * Two-stage method with only one matrix factorization per step.
 *
 * Algorithm, with f_t = df/dt at (y_n, t_n) when opts.timeDerivative is set:
 *   (I - gamma*dt*J) * k1 = f(y_n, t_n)               + gamma*dt*f_t
 *   (I - gamma*dt*J) * k2 = f(y_n + dt*k1, t_n + dt) - 2*k1 - gamma*dt*f_t
 *   y_{n+1} = y_n + (3/2)*dt*k1 + (1/2)*dt*k2
 *
 * @tparam State State type.
 *
 * @note RT-safe with RT-safe callbacks.
 */
template <typename State> class ROS2 {
public:
  struct Stats {
    std::size_t functionEvals{0};
    std::size_t matrixEvals{0};
    std::size_t linearSolves{0};
    std::size_t steps{0};
  };

  // ROS2 coefficient: 1 + 1/sqrt(2) = 1.7071067811865476
  static constexpr double GAMMA = 1.7071067811865476;

  /**
   * @brief Initialize the integrator.
   *
   * @tparam Func Derivative function: State(const State& y, double t)
   * @param f Derivative function.
   * @param y0 Initial state.
   * @param t0 Initial time.
   */
  template <typename Func>
  void initialize(Func&& f, [[maybe_unused]] const State& y0, double t0) noexcept {
    f_ = std::forward<Func>(f);
    t_ = t0;
    stats_ = Stats{};
  }

  /**
   * @brief Perform one ROS2 step.
   *
   * @param y State (modified in place).
   * @param dt Time step.
   * @param opts Rosenbrock options.
   * @return Status code.
   */
  uint8_t step(State& y, double dt, const RosenbrockOptions<State>& opts) noexcept {
    if (!isValidStep(dt)) {
      return static_cast<uint8_t>(RosenbrockStatus::ERROR_INVALID_STEP);
    }

    const double GAMMA_DT = GAMMA * dt;

    // Compute matrix (I - gamma*dt*J)
    auto matrix = opts.computeMatrix(y, t_, GAMMA_DT);
    ++stats_.matrixEvals;

    // Stage 1: (I - gamma*dt*J) * k1 = f(y) + gamma*dt*f_t
    State rhs1 = f_(y, t_);
    ++stats_.functionEvals;

    // The time-derivative terms of the two stages are equal and opposite
    State ftTerm{};
    if (opts.timeDerivative) {
      ftTerm = opts.timeDerivative(y, t_) * GAMMA_DT;
      rhs1 = rhs1 + ftTerm;
    }

    State k1 = opts.linearSolve(matrix, rhs1);
    ++stats_.linearSolves;

    // Stage 2: (I - gamma*dt*J) * k2 = f(y + dt*k1) - 2*k1 - gamma*dt*f_t
    State y1 = y + k1 * dt;
    State f1 = f_(y1, t_ + dt);
    ++stats_.functionEvals;

    State rhs2 = f1 - k1 * 2.0 - ftTerm;
    State k2 = opts.linearSolve(matrix, rhs2);
    ++stats_.linearSolves;

    // Update: y_{n+1} = y_n + (3/2)*dt*k1 + (1/2)*dt*k2
    y = y + k1 * (1.5 * dt) + k2 * (0.5 * dt);

    t_ += dt;
    ++stats_.steps;

    return static_cast<uint8_t>(RosenbrockStatus::SUCCESS);
  }

  double time() const noexcept { return t_; }
  const Stats& stats() const noexcept { return stats_; }
  void reset(double t0 = 0.0) noexcept {
    t_ = t0;
    stats_ = Stats{};
  }

private:
  std::function<State(const State&, double)> f_;
  double t_{0.0};
  Stats stats_{};
};

/* ---------------------------------- ROS3 ---------------------------------- */

/**
 * @brief ROS3: Third-order L-stable Rosenbrock method.
 *
 * Three stages, one matrix factorization and two derivative evaluations per
 * step (the third stage is evaluated at the second stage's point). This is
 * the method named ROS3 by Sandu, Verwer, Blom, Spee, Carmichael and Potra
 * (1997).
 *
 * Algorithm, with W = I - gamma*dt*J, f_t = df/dt at (y_n, t_n) when
 * opts.timeDerivative is set, and each k_i in the units of f:
 *   W * k1 = f(y_n, t_n)                              + dt*D1*f_t
 *   W * k2 = f(y_n + dt*A21*k1, t_n + gamma*dt)       + C21*k1          + dt*D2*f_t
 *   W * k3 = f(y_n + dt*A21*k1, t_n + gamma*dt)       + C31*k1 + C32*k2 + dt*D3*f_t
 *   y_{n+1} = y_n + dt*(M1*k1 + M2*k2 + M3*k3)
 *
 * This is the form that needs no product of the Jacobian with a vector,
 * only solves with W, which is all the options provide. Its coefficients
 * follow from the method's defining ones (gamma_ij, alpha_ij, b_i) by
 * C = -gamma*offdiag(G^-1), A = gamma*alpha*G^-1, M = gamma*b*G^-1, with G
 * the lower-triangular matrix of gamma_ij; D_i is the i-th row sum of G.
 *
 * Third order needs opts.timeDerivative when f depends on time. On a stiff
 * problem at a step far above its fastest time constant the observed order
 * is below three until the step is small enough to resolve it.
 *
 * @tparam State State type.
 *
 * @note RT-safe with RT-safe callbacks.
 */
template <typename State> class ROS3 {
public:
  struct Stats {
    std::size_t functionEvals{0};
    std::size_t matrixEvals{0};
    std::size_t linearSolves{0};
    std::size_t steps{0};
  };

  // The root of g^3 - 3g^2 + (3/2)g - 1/6 = 0 for which the method is L-stable
  static constexpr double GAMMA = 0.435866521508458999416;
  static constexpr double A21 = GAMMA;
  static constexpr double C21 = -0.442673496217456978751;
  static constexpr double C31 = 1.776590043580387551791;
  static constexpr double C32 = 4.013319204246335278389;
  static constexpr double M1 = GAMMA;
  static constexpr double M2 = 2.689206956220652834790;
  static constexpr double M3 = -0.186429946765601044632;
  static constexpr double D1 = GAMMA;
  static constexpr double D2 = 0.242919964548168043666;
  static constexpr double D3 = 2.185138002766405851135;

  /**
   * @brief Initialize the integrator.
   *
   * @tparam Func Derivative function: State(const State& y, double t)
   * @param f Derivative function.
   * @param y0 Initial state.
   * @param t0 Initial time.
   */
  template <typename Func>
  void initialize(Func&& f, [[maybe_unused]] const State& y0, double t0) noexcept {
    f_ = std::forward<Func>(f);
    t_ = t0;
    stats_ = Stats{};
  }

  /**
   * @brief Perform one ROS3 step.
   *
   * @param y State (modified in place).
   * @param dt Time step.
   * @param opts Rosenbrock options.
   * @return Status code.
   */
  uint8_t step(State& y, double dt, const RosenbrockOptions<State>& opts) noexcept {
    if (!isValidStep(dt)) {
      return static_cast<uint8_t>(RosenbrockStatus::ERROR_INVALID_STEP);
    }

    const double GAMMA_DT = GAMMA * dt;

    // Compute matrix (I - gamma*dt*J)
    auto matrix = opts.computeMatrix(y, t_, GAMMA_DT);
    ++stats_.matrixEvals;

    // Stage 1
    State rhs1 = f_(y, t_);
    ++stats_.functionEvals;

    // Stages 2 and 3 share one derivative: both are evaluated at y_n + dt*A21*k1
    State rhs2{};
    State rhs3{};
    if (opts.timeDerivative) {
      const State ft = opts.timeDerivative(y, t_);
      rhs1 = rhs1 + ft * (D1 * dt);
      rhs2 = ft * (D2 * dt);
      rhs3 = ft * (D3 * dt);
    }

    const State k1 = opts.linearSolve(matrix, rhs1);

    const State f2 = f_(y + k1 * (A21 * dt), t_ + GAMMA_DT);
    ++stats_.functionEvals;

    const State k2 = opts.linearSolve(matrix, rhs2 + f2 + k1 * C21);
    const State k3 = opts.linearSolve(matrix, rhs3 + f2 + k1 * C31 + k2 * C32);
    stats_.linearSolves += 3;

    y = y + (k1 * M1 + k2 * M2 + k3 * M3) * dt;

    t_ += dt;
    ++stats_.steps;

    return static_cast<uint8_t>(RosenbrockStatus::SUCCESS);
  }

  double time() const noexcept { return t_; }
  const Stats& stats() const noexcept { return stats_; }
  void reset(double t0 = 0.0) noexcept {
    t_ = t0;
    stats_ = Stats{};
  }

private:
  std::function<State(const State&, double)> f_;
  double t_{0.0};
  Stats stats_{};
};

} // namespace integration
} // namespace math
} // namespace apex

#endif // APEX_MATH_INTEGRATION_ROSENBROCK_HPP
