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

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace apex {
namespace math {
namespace integration {

/* -------------------------------- Status ---------------------------------- */

enum class RKNStatus : uint8_t { SUCCESS = 0, ERROR_INVALID_STEP = 1 };

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
   * @param dt Time step (must be positive).
   * @param accel Acceleration function of (time, position, velocity).
   * @return Status code.
   * @note RT-safe: O(1), 4 function evaluations.
   */
  template <typename AccelFunc>
  uint8_t step(State& y, State& v, double dt, AccelFunc&& accel) noexcept {
    if (dt <= 0.0) {
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
 * Higher accuracy for smooth problems with 7 evaluations per step.
 * Coefficients from Dormand-Prince-Nystrom family.
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
    if (dt <= 0.0) {
      return static_cast<uint8_t>(RKNStatus::ERROR_INVALID_STEP);
    }

    const double DT_SQ = dt * dt;

    // Stage 1
    State k1 = accel(t_, y, v);
    ++stats_.functionEvals;

    // Stage 2: c2 = 1/10
    constexpr double C2 = 0.1;
    constexpr double A21 = 0.005; // 1/200
    constexpr double B21 = 0.1;
    State y2 = y + v * (C2 * dt) + k1 * (A21 * DT_SQ);
    State v2 = v + k1 * (B21 * dt);
    State k2 = accel(t_ + C2 * dt, y2, v2);
    ++stats_.functionEvals;

    // Stage 3: c3 = 1/5
    constexpr double C3 = 0.2;
    constexpr double A31 = 2.0 / 75.0;
    constexpr double A32 = 4.0 / 75.0;
    constexpr double B31 = 1.0 / 15.0;
    constexpr double B32 = 2.0 / 15.0;
    State y3 = y + v * (C3 * dt) + (k1 * A31 + k2 * A32) * DT_SQ;
    State v3 = v + (k1 * B31 + k2 * B32) * dt;
    State k3 = accel(t_ + C3 * dt, y3, v3);
    ++stats_.functionEvals;

    // Stage 4: c4 = 3/5
    constexpr double C4 = 0.6;
    constexpr double A41 = 171.0 / 1960.0;
    constexpr double A42 = 171.0 / 490.0;
    constexpr double A43 = 27.0 / 392.0;
    constexpr double B41 = 12.0 / 35.0;
    constexpr double B42 = 6.0 / 35.0;
    constexpr double B43 = 6.0 / 35.0;
    State y4 = y + v * (C4 * dt) + (k1 * A41 + k2 * A42 + k3 * A43) * DT_SQ;
    State v4 = v + (k1 * B41 + k2 * B42 + k3 * B43) * dt;
    State k4 = accel(t_ + C4 * dt, y4, v4);
    ++stats_.functionEvals;

    // Stage 5: c5 = 2/3
    constexpr double C5 = 2.0 / 3.0;
    constexpr double A51 = 32.0 / 405.0;
    constexpr double A52 = 320.0 / 2187.0;
    constexpr double A53 = 8.0 / 729.0;
    constexpr double A54 = 152.0 / 2187.0;
    constexpr double B51 = 16.0 / 81.0;
    constexpr double B53 = 4.0 / 27.0;
    constexpr double B54 = 16.0 / 81.0;
    State y5 = y + v * (C5 * dt) + (k1 * A51 + k2 * A52 + k3 * A53 + k4 * A54) * DT_SQ;
    State v5 = v + (k1 * B51 + k3 * B53 + k4 * B54) * dt;
    State k5 = accel(t_ + C5 * dt, y5, v5);
    ++stats_.functionEvals;

    // Stage 6: c6 = 4/5
    constexpr double C6 = 0.8;
    constexpr double A61 = 3953.0 / 26040.0;
    constexpr double A62 = 400.0 / 1989.0;
    constexpr double A64 = 2581.0 / 3315.0;
    constexpr double A65 = -9.0 / 26.0;
    constexpr double B61 = 25.0 / 51.0;
    constexpr double B64 = 100.0 / 153.0;
    constexpr double B65 = -125.0 / 306.0;
    State y6 = y + v * (C6 * dt) + (k1 * A61 + k2 * A62 + k4 * A64 + k5 * A65) * DT_SQ;
    State v6 = v + (k1 * B61 + k4 * B64 + k5 * B65) * dt;
    State k6 = accel(t_ + C6 * dt, y6, v6);
    ++stats_.functionEvals;

    // Stage 7: c7 = 1
    constexpr double A71 = 1593.0 / 10920.0;
    constexpr double A74 = 737.0 / 936.0;
    constexpr double A75 = -11.0 / 52.0;
    constexpr double A76 = 143.0 / 780.0;
    constexpr double B71 = 1.0 / 6.0;
    constexpr double B74 = 2.0 / 3.0;
    constexpr double B76 = 1.0 / 6.0;
    State y7 = y + v * dt + (k1 * A71 + k4 * A74 + k5 * A75 + k6 * A76) * DT_SQ;
    State v7 = v + (k1 * B71 + k4 * B74 + k6 * B76) * dt;
    State k7 = accel(t_ + dt, y7, v7);
    ++stats_.functionEvals;

    // Final update using 6th-order weights
    constexpr double BY1 = 1.0 / 20.0;
    constexpr double BY4 = 64.0 / 75.0;
    constexpr double BY6 = 1.0 / 20.0;
    constexpr double BY7 = 2.0 / 75.0;

    constexpr double BV1 = 1.0 / 12.0;
    constexpr double BV4 = 64.0 / 75.0;
    constexpr double BV5 = -27.0 / 100.0;
    constexpr double BV7 = 1.0 / 6.0;

    y = y + v * dt + (k1 * BY1 + k4 * BY4 + k6 * BY6 + k7 * BY7) * DT_SQ;
    v = v + (k1 * BV1 + k4 * BV4 + k5 * BV5 + k7 * BV7) * dt;

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
   * @return Result with status, error, and suggested next dt.
   */
  template <typename AccelFunc>
  Result step(State& y, State& v, double dt, AccelFunc&& accel, const Options& opts) noexcept {
    Result result;

    if (dt <= 0.0) {
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

  double computeNorm(const State& errY, const State& errV, const State& y, const State& v,
                     const Options& opts) const noexcept {
    if constexpr (std::is_arithmetic_v<State>) {
      double scaleY = opts.absTol + opts.relTol * std::abs(y);
      double scaleV = opts.absTol + opts.relTol * std::abs(v);
      double normY = std::abs(errY) / scaleY;
      double normV = std::abs(errV) / scaleV;
      return std::max(normY, normV);
    } else {
      double scaleY = opts.absTol + opts.relTol * y.normInf();
      double scaleV = opts.absTol + opts.relTol * v.normInf();
      double normY = errY.normInf() / scaleY;
      double normV = errV.normInf() / scaleV;
      return std::max(normY, normV);
    }
  }
};

} // namespace integration
} // namespace math
} // namespace apex

#endif // APEX_MATH_INTEGRATION_RUNGEKUTTANYSTROM_HPP
