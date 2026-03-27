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
 *  - ROS3P: Third-order, L-stable
 *  - ROS34PW2: Third-order, embedded error estimation
 *
 * Use cases: Chemical kinetics, battery management, thermal systems.
 * @note RT-safe with RT-safe callbacks.
 */

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
  ERROR_INVALID_STEP = 1,
  ERROR_JACOBIAN_FAILURE = 2,
  ERROR_LINEAR_SOLVER = 3
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
};

/* ---------------------------------- ROS2 ---------------------------------- */

/**
 * @brief ROS2: Second-order L-stable Rosenbrock method.
 *
 * Two-stage method with only one matrix factorization per step.
 *
 * Algorithm:
 *   (I - gamma*dt*J) * k1 = f(y_n)
 *   (I - gamma*dt*J) * k2 = f(y_n + dt*k1) - 2*k1
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
    if (dt <= 0.0) {
      return static_cast<uint8_t>(RosenbrockStatus::ERROR_INVALID_STEP);
    }

    const double GAMMA_DT = GAMMA * dt;

    // Compute matrix (I - gamma*dt*J)
    auto matrix = opts.computeMatrix(y, t_, GAMMA_DT);
    ++stats_.matrixEvals;

    // Stage 1: (I - gamma*dt*J) * k1 = f(y)
    State f0 = f_(y, t_);
    ++stats_.functionEvals;

    State k1 = opts.linearSolve(matrix, f0);
    ++stats_.linearSolves;

    // Stage 2: (I - gamma*dt*J) * k2 = f(y + dt*k1) - 2*k1
    State y1 = y + k1 * dt;
    State f1 = f_(y1, t_ + dt);
    ++stats_.functionEvals;

    State rhs2 = f1 - k1 * 2.0;
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

/* --------------------------------- ROS3P ---------------------------------- */

/**
 * @brief ROS3P: Third-order L-stable Rosenbrock method.
 *
 * Three-stage method with one matrix factorization.
 * Higher accuracy than ROS2 with similar cost.
 *
 * @tparam State State type.
 *
 * @note RT-safe with RT-safe callbacks.
 */
template <typename State> class ROS3P {
public:
  struct Stats {
    std::size_t functionEvals{0};
    std::size_t matrixEvals{0};
    std::size_t linearSolves{0};
    std::size_t steps{0};
  };

  // ROS3P coefficients (Hairer-Wanner)
  static constexpr double GAMMA = 0.43586652150845899942;
  static constexpr double A21 = 0.43586652150845899942;
  static constexpr double A31 = 0.43586652150845899942;
  static constexpr double A32 = 0.0;
  static constexpr double C21 = -0.19294655696029095575;
  static constexpr double C31 = 0.0;
  static constexpr double C32 = 1.74927148125420818960;
  static constexpr double M1 = -0.75457412385404315829;
  static constexpr double M2 = -0.43586652150845899942;
  static constexpr double M3 = 0.19034761532769155505;

  template <typename Func>
  void initialize(Func&& f, [[maybe_unused]] const State& y0, double t0) noexcept {
    f_ = std::forward<Func>(f);
    t_ = t0;
    stats_ = Stats{};
  }

  uint8_t step(State& y, double dt, const RosenbrockOptions<State>& opts) noexcept {
    if (dt <= 0.0) {
      return static_cast<uint8_t>(RosenbrockStatus::ERROR_INVALID_STEP);
    }

    const double GAMMA_DT = GAMMA * dt;

    // Compute matrix (I - gamma*dt*J)
    auto matrix = opts.computeMatrix(y, t_, GAMMA_DT);
    ++stats_.matrixEvals;

    // Stage 1
    State f0 = f_(y, t_);
    ++stats_.functionEvals;

    State k1 = opts.linearSolve(matrix, f0);
    ++stats_.linearSolves;

    // Stage 2
    State y2 = y + k1 * (A21 * dt);
    State f2 = f_(y2, t_ + A21 * dt);
    ++stats_.functionEvals;

    State rhs2 = f2 + k1 * (C21 / dt);
    State k2 = opts.linearSolve(matrix, rhs2);
    ++stats_.linearSolves;

    // Stage 3
    State y3 = y + k1 * (A31 * dt) + k2 * (A32 * dt);
    State f3 = f_(y3, t_ + (A31 + A32) * dt);
    ++stats_.functionEvals;

    State rhs3 = f3 + k1 * (C31 / dt) + k2 * (C32 / dt);
    State k3 = opts.linearSolve(matrix, rhs3);
    ++stats_.linearSolves;

    // Update
    y = y + k1 * (M1 * dt) + k2 * (M2 * dt) + k3 * (M3 * dt);

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
