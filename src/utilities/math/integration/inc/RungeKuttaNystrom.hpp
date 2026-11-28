#ifndef APEX_MATH_INTEGRATION_RUNGEKUTTANYSTROM_HPP
#define APEX_MATH_INTEGRATION_RUNGEKUTTANYSTROM_HPP
/**
 * @file RungeKuttaNystrom.hpp
 * @brief Runge-Kutta-Nystrom methods for second-order ODEs.
 *
 * Direct integration of y'' = f(t, y, y') without reduction to first-order.
 * More efficient than standard RK for second-order systems.
 *
 * Methods:
 *  - RKN4: Fourth-order, 4 evaluations per step
 *  - RKN6: Sixth-order, 7 evaluations per step
 *
 * Use cases: Mechanical vibrations, orbital mechanics, structural dynamics.
 * @note RT-safe: Zero allocation, fixed evaluations per step.
 */

#include "src/utilities/math/integration/inc/StepSize.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace apex {
namespace math {
namespace integration {

/* -------------------------------- Status ---------------------------------- */

enum class RKNStatus : uint8_t {
  SUCCESS = 0,
  ERROR_INVALID_STEP = 1, ///< Step size is not positive and finite.
  STEP_REJECTED = 2,      ///< Adaptive step rejected: state and time unchanged, retry with dtNext.
  ERROR_NON_FINITE = 3    ///< The acceleration produced a NaN or the step overflowed.
};

/* ---------------------------------- RKN4 ---------------------------------- */

/**
 * @brief Fourth-order Runge-Kutta-Nystrom integrator.
 *
 * For second-order ODEs: y'' = f(t, y, y')
 * Updates both position and velocity in a single step.
 *
 * Algorithm (4-stage, 4th order):
 *   k1 = f(t_n, y_n, y'_n)
 *   k2 = f(t_n + h/2, y_n + h*y'_n/2 + h^2*k1/8, y'_n + h*k1/2)
 *   k3 = f(t_n + h/2, y_n + h*y'_n/2 + h^2*k1/8, y'_n + h*k2/2)
 *   k4 = f(t_n + h, y_n + h*y'_n + h^2*k3/2, y'_n + h*k3)
 *   y_{n+1} = y_n + h*y'_n + h^2*(k1 + k2 + k3)/6
 *   y'_{n+1} = y'_n + h*(k1 + 2*k2 + 2*k3 + k4)/6
 *
 * The third stage's position uses k1: with k2 there the rule is third
 * order.
 *
 * @tparam State State type for position and velocity.
 *
 * Example:
 * @code
 *   // Simple harmonic oscillator: y'' = -y
 *   auto f = [](double t, const double& y, const double& v) {
 *     return -y;  // Acceleration
 *   };
 *
 *   RKN4<double> integrator;
 *   double y = 1.0, v = 0.0;
 *   for (int i = 0; i < 1000; ++i) {
 *     integrator.step(y, v, 0.01, f);
 *   }
 * @endcode
 *
 * @note RT-safe: Zero allocation, 4 function evaluations per step.
 */
template <typename State> class RKN4 {
public:
  struct Stats {
    std::size_t functionEvals{0};
    std::size_t steps{0};
  };

  /**
   * @brief Perform one RKN4 step.
   *
   * @tparam AccelFunc Callable: State(double t, const State& y, const State& v)
   * @param y Position (modified in place).
   * @param v Velocity (modified in place).
   * @param dt Time step (positive and finite).
   * @param accel Acceleration function of (time, position, velocity).
   * @return Status code.
   * @note RT-safe: O(1), 4 function evaluations.
   */
  template <typename AccelFunc>
  uint8_t step(State& y, State& v, double dt, AccelFunc&& accel) noexcept {
    if (!isValidStep(dt)) {
      return static_cast<uint8_t>(RKNStatus::ERROR_INVALID_STEP);
    }

    const double HALF_DT = 0.5 * dt;
    const double DT_SQ = dt * dt;

    // Stage 1
    State k1 = accel(t_, y, v);
    ++stats_.functionEvals;

    // Stage 2
    State y2 = y + v * HALF_DT + k1 * (DT_SQ * 0.125);
    State v2 = v + k1 * HALF_DT;
    State k2 = accel(t_ + HALF_DT, y2, v2);
    ++stats_.functionEvals;

    // Stage 3
    State y3 = y + v * HALF_DT + k1 * (DT_SQ * 0.125);
    State v3 = v + k2 * HALF_DT;
    State k3 = accel(t_ + HALF_DT, y3, v3);
    ++stats_.functionEvals;

    // Stage 4
    State y4 = y + v * dt + k3 * (DT_SQ * 0.5);
    State v4 = v + k3 * dt;
    State k4 = accel(t_ + dt, y4, v4);
    ++stats_.functionEvals;

    // Update position: y_{n+1} = y_n + h*v_n + h^2*(k1 + k2 + k3)/6
    y = y + v * dt + (k1 + k2 + k3) * (DT_SQ / 6.0);

    // Update velocity: v_{n+1} = v_n + h*(k1 + 2*k2 + 2*k3 + k4)/6
    v = v + (k1 + k2 * 2.0 + k3 * 2.0 + k4) * (dt / 6.0);

    t_ += dt;
    ++stats_.steps;

    return static_cast<uint8_t>(RKNStatus::SUCCESS);
  }

  void reset(double t0 = 0.0) noexcept {
    stats_ = Stats{};
    t_ = t0;
  }

  double time() const noexcept { return t_; }
  const Stats& stats() const noexcept { return stats_; }

private:
  Stats stats_{};
  double t_{0.0};
};

/* ---------------------------------- RKN6 ---------------------------------- */

/**
 * @brief Sixth-order Runge-Kutta-Nystrom integrator.
 *
 * Seven evaluations per step, for an acceleration that may depend on
 * position, velocity and time. The step is a seven-stage sixth-order
 * explicit Runge-Kutta method applied to y' = v, v' = f(t, y, v) and
 * written in Nystrom form: with A, b, c the Runge-Kutta coefficients, the
 * velocity stages use A and b and the position stages use A.A and b.A:
 *
 *   y_i = y + c_i*dt*v + dt^2 * sum_j (A.A)_ij * k_j
 *   v_i = v + dt * sum_j A_ij * k_j,        k_i = f(t + c_i*dt, y_i, v_i)
 *   y+  = y + dt*v + dt^2 * sum_i (b.A)_i * k_i
 *   v+  = v + dt * sum_i b_i * k_i
 *
 * Nodes c = (0, 1/2, 2/3, 1/3, 5/6, 1/6, 1), weights
 * b = (13/200, 0, 11/40, 11/40, 4/25, 4/25, 13/200): every node lies inside
 * the step and no weight is negative. The velocity coefficients below are
 * the array A; the position coefficients are its products A.A and b.A. The
 * array satisfies all 37 order conditions of a sixth-order method (one per
 * rooted tree of up to six nodes).
 *
 * @tparam State State type.
 *
 * @note RT-safe: Zero allocation, 7 function evaluations per step.
 */
template <typename State> class RKN6 {
public:
  struct Stats {
    std::size_t functionEvals{0};
    std::size_t steps{0};
  };

  /**
   * @brief Perform one RKN6 step.
   *
   * @tparam AccelFunc Callable: State(double t, const State& y, const State& v)
   * @param y Position (modified in place).
   * @param v Velocity (modified in place).
   * @param dt Time step.
   * @param accel Acceleration function.
   * @return Status code.
   */
  template <typename AccelFunc>
  uint8_t step(State& y, State& v, double dt, AccelFunc&& accel) noexcept {
    if (!isValidStep(dt)) {
      return static_cast<uint8_t>(RKNStatus::ERROR_INVALID_STEP);
    }

    const double DT_SQ = dt * dt;

    // Nodes
    constexpr double C2 = 1.0 / 2.0;
    constexpr double C3 = 2.0 / 3.0;
    constexpr double C4 = 1.0 / 3.0;
    constexpr double C5 = 5.0 / 6.0;
    constexpr double C6 = 1.0 / 6.0;

    // Stage 1
    const State k1 = accel(t_, y, v);

    // Stage 2. Its position has no acceleration term: A.A is zero in this row.
    constexpr double B21 = 1.0 / 2.0;
    const State y2 = y + v * (C2 * dt);
    const State v2 = v + k1 * (B21 * dt);
    const State k2 = accel(t_ + C2 * dt, y2, v2);

    // Stage 3
    constexpr double A31 = 2.0 / 9.0;
    constexpr double B31 = 2.0 / 9.0;
    constexpr double B32 = 4.0 / 9.0;
    const State y3 = y + v * (C3 * dt) + k1 * (A31 * DT_SQ);
    const State v3 = v + (k1 * B31 + k2 * B32) * dt;
    const State k3 = accel(t_ + C3 * dt, y3, v3);

    // Stage 4
    constexpr double A41 = 5.0 / 54.0;
    constexpr double A42 = -1.0 / 27.0;
    constexpr double B41 = 7.0 / 36.0;
    constexpr double B42 = 2.0 / 9.0;
    constexpr double B43 = -1.0 / 12.0;
    const State y4 = y + v * (C4 * dt) + (k1 * A41 + k2 * A42) * DT_SQ;
    const State v4 = v + (k1 * B41 + k2 * B42 + k3 * B43) * dt;
    const State k4 = accel(t_ + C4 * dt, y4, v4);

    // Stage 5
    constexpr double A51 = -205.0 / 864.0;
    constexpr double A52 = 20.0 / 27.0;
    constexpr double A53 = -5.0 / 32.0;
    constexpr double B51 = -35.0 / 144.0;
    constexpr double B52 = -55.0 / 36.0;
    constexpr double B53 = 35.0 / 48.0;
    constexpr double B54 = 15.0 / 8.0;
    const State y5 = y + v * (C5 * dt) + (k1 * A51 + k2 * A52 + k3 * A53) * DT_SQ;
    const State v5 = v + (k1 * B51 + k2 * B52 + k3 * B53 + k4 * B54) * dt;
    const State k5 = accel(t_ + C5 * dt, y5, v5);

    // Stage 6
    constexpr double A61 = -31.0 / 288.0;
    constexpr double A62 = -7.0 / 72.0;
    constexpr double A63 = 1.0 / 32.0;
    constexpr double A64 = 3.0 / 16.0;
    constexpr double B61 = -1.0 / 360.0;
    constexpr double B62 = -11.0 / 36.0;
    constexpr double B63 = -1.0 / 8.0;
    constexpr double B64 = 1.0 / 2.0;
    constexpr double B65 = 1.0 / 10.0;
    const State y6 = y + v * (C6 * dt) + (k1 * A61 + k2 * A62 + k3 * A63 + k4 * A64) * DT_SQ;
    const State v6 = v + (k1 * B61 + k2 * B62 + k3 * B63 + k4 * B64 + k5 * B65) * dt;
    const State k6 = accel(t_ + C6 * dt, y6, v6);

    // Stage 7, at the end of the step
    constexpr double A71 = 32.0 / 117.0;
    constexpr double A72 = -167.0 / 117.0;
    constexpr double A73 = 3.0 / 26.0;
    constexpr double A74 = 4.0 / 3.0;
    constexpr double A75 = 8.0 / 39.0;
    constexpr double B71 = -41.0 / 260.0;
    constexpr double B72 = 22.0 / 13.0;
    constexpr double B73 = 43.0 / 156.0;
    constexpr double B74 = -118.0 / 39.0;
    constexpr double B75 = 32.0 / 195.0;
    constexpr double B76 = 80.0 / 39.0;
    const State y7 = y + v * dt + (k1 * A71 + k2 * A72 + k3 * A73 + k4 * A74 + k5 * A75) * DT_SQ;
    const State v7 = v + (k1 * B71 + k2 * B72 + k3 * B73 + k4 * B74 + k5 * B75 + k6 * B76) * dt;
    const State k7 = accel(t_ + dt, y7, v7);

    stats_.functionEvals += 7;

    // Position weights (b.A) and velocity weights (b)
    constexpr double BY1 = 13.0 / 200.0;
    constexpr double BY3 = 11.0 / 120.0;
    constexpr double BY4 = 11.0 / 60.0;
    constexpr double BY5 = 2.0 / 75.0;
    constexpr double BY6 = 2.0 / 15.0;

    constexpr double BV1 = 13.0 / 200.0;
    constexpr double BV3 = 11.0 / 40.0;
    constexpr double BV4 = 11.0 / 40.0;
    constexpr double BV5 = 4.0 / 25.0;
    constexpr double BV6 = 4.0 / 25.0;
    constexpr double BV7 = 13.0 / 200.0;

    const State yNew = y + v * dt + (k1 * BY1 + k3 * BY3 + k4 * BY4 + k5 * BY5 + k6 * BY6) * DT_SQ;
    v = v + (k1 * BV1 + k3 * BV3 + k4 * BV4 + k5 * BV5 + k6 * BV6 + k7 * BV7) * dt;
    y = yNew;

    t_ += dt;
    ++stats_.steps;

    return static_cast<uint8_t>(RKNStatus::SUCCESS);
  }

  void reset(double t0 = 0.0) noexcept {
    stats_ = Stats{};
    t_ = t0;
  }

  double time() const noexcept { return t_; }
  const Stats& stats() const noexcept { return stats_; }

private:
  Stats stats_{};
  double t_{0.0};
};

/* -------------------------------- RKN34 ----------------------------------- */

/**
 * @brief Adaptive RKN3(4) integrator with embedded error estimation.
 *
 * Third-order method with fourth-order error estimate.
 * Allows step size control for second-order problems.
 *
 * @tparam State State type.
 *
 * @note RT-safe: Zero allocation, 4 function evaluations per step.
 */
template <typename State> class RKN34 {
public:
  struct Stats {
    std::size_t functionEvals{0};
    std::size_t acceptedSteps{0};
    std::size_t rejectedSteps{0};
  };

  struct Options {
    double absTol = 1e-6;
    double relTol = 1e-6;
    double dtMin = 1e-12;
    double dtMax = 1.0;
    double safetyFactor = 0.9;
    double maxGrowth = 5.0;
    double maxShrink = 0.1;
  };

  struct Result {
    RKNStatus status{RKNStatus::SUCCESS};
    double errorEstimate{0.0};
    double dtNext{0.0};
  };

  /**
   * @brief Attempt one adaptive RKN step.
   *
   * @tparam AccelFunc Callable: State(double t, const State& y, const State& v)
   * @param y Position (modified if accepted).
   * @param v Velocity (modified if accepted).
   * @param dt Proposed step size.
   * @param accel Acceleration function.
   * @param opts Tolerance options.
   * @return Result with status, error, and suggested next dt. SUCCESS only
   *         when the step was taken. STEP_REJECTED when the error estimate
   *         exceeds the tolerance, and ERROR_NON_FINITE when it is NaN or
   *         infinite: in both, y, v and time() are unchanged, and dtNext is
   *         a smaller step to retry with (no lower than dtMin).
   */
  template <typename AccelFunc>
  Result step(State& y, State& v, double dt, AccelFunc&& accel, const Options& opts) noexcept {
    Result result;

    if (!isValidStep(dt)) {
      result.status = RKNStatus::ERROR_INVALID_STEP;
      result.dtNext = opts.dtMin;
      return result;
    }

    const double DT_SQ = dt * dt;
    const double HALF_DT = 0.5 * dt;

    // Stage evaluations (same as RKN4)
    State k1 = accel(t_, y, v);
    ++stats_.functionEvals;

    State y2 = y + v * HALF_DT + k1 * (DT_SQ * 0.125);
    State v2 = v + k1 * HALF_DT;
    State k2 = accel(t_ + HALF_DT, y2, v2);
    ++stats_.functionEvals;

    State y3 = y + v * HALF_DT + k1 * (DT_SQ * 0.125);
    State v3 = v + k2 * HALF_DT;
    State k3 = accel(t_ + HALF_DT, y3, v3);
    ++stats_.functionEvals;

    State y4 = y + v * dt + k3 * (DT_SQ * 0.5);
    State v4 = v + k3 * dt;
    State k4 = accel(t_ + dt, y4, v4);
    ++stats_.functionEvals;

    // Fourth-order solution
    State yNew = y + v * dt + (k1 + k2 + k3) * (DT_SQ / 6.0);
    State vNew = v + (k1 + k2 * 2.0 + k3 * 2.0 + k4) * (dt / 6.0);

    // Third-order solution for error estimate
    State yLow = y + v * dt + (k1 + k2 * 2.0 + k3) * (DT_SQ / 8.0);
    State vLow = v + (k1 + k2 * 3.0 + k3 * 3.0 + k4) * (dt / 8.0);

    // Error estimation
    State errY = yNew - yLow;
    State errV = vNew - vLow;

    result.errorEstimate = computeNorm(errY, errV, yNew, vNew, opts);

    // A NaN compares false with everything: without this test it would be
    // handled as a rejection with a NaN step suggestion.
    if (!std::isfinite(result.errorEstimate)) {
      result.status = RKNStatus::ERROR_NON_FINITE;
      result.dtNext = std::max(dt * opts.maxShrink, opts.dtMin);
      return result;
    }

    if (result.errorEstimate <= 1.0) {
      // Accept step
      y = yNew;
      v = vNew;
      t_ += dt;
      ++stats_.acceptedSteps;

      double factor = opts.safetyFactor * std::pow(result.errorEstimate, -0.25);
      factor = std::min(factor, opts.maxGrowth);
      factor = std::max(factor, opts.maxShrink);
      result.dtNext = std::min(dt * factor, opts.dtMax);
    } else {
      // Reject step
      result.status = RKNStatus::STEP_REJECTED;
      ++stats_.rejectedSteps;

      double factor = opts.safetyFactor * std::pow(result.errorEstimate, -0.2);
      factor = std::max(factor, opts.maxShrink);
      result.dtNext = std::max(dt * factor, opts.dtMin);
    }

    return result;
  }

  void reset(double t0 = 0.0) noexcept {
    stats_ = Stats{};
    t_ = t0;
  }

  double time() const noexcept { return t_; }
  const Stats& stats() const noexcept { return stats_; }

private:
  Stats stats_{};
  double t_{0.0};

  // The larger of two error norms, NaN when either is NaN. std::max returns
  // its first argument whenever the comparison is false, which drops a NaN
  // held by the second.
  static double largerKeepingNan(double a, double b) noexcept {
    return (a > b || std::isnan(a)) ? a : b;
  }

  double computeNorm(const State& errY, const State& errV, const State& y, const State& v,
                     const Options& opts) const noexcept {
    if constexpr (std::is_arithmetic_v<State>) {
      double scaleY = opts.absTol + opts.relTol * std::abs(y);
      double scaleV = opts.absTol + opts.relTol * std::abs(v);
      double normY = std::abs(errY) / scaleY;
      double normV = std::abs(errV) / scaleV;
      return largerKeepingNan(normY, normV);
    } else {
      double scaleY = opts.absTol + opts.relTol * y.normInf();
      double scaleV = opts.absTol + opts.relTol * v.normInf();
      double normY = errY.normInf() / scaleY;
      double normV = errV.normInf() / scaleV;
      return largerKeepingNan(normY, normV);
    }
  }
};

} // namespace integration
} // namespace math
} // namespace apex

#endif // APEX_MATH_INTEGRATION_RUNGEKUTTANYSTROM_HPP
