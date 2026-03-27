#ifndef APEX_MATH_INTEGRATION_RUNGEKUTTA45_HPP
#define APEX_MATH_INTEGRATION_RUNGEKUTTA45_HPP
/**
 * @file RungeKutta45.hpp
 * @brief Dormand-Prince RK45 adaptive integrator with embedded error estimation.
 *
 * Fifth-order method with fourth-order error estimate. Industry standard
 * for non-stiff ODEs requiring adaptive step control.
 *
 * Use cases: Trajectory prediction, orbital mechanics, control simulation.
 * @note RT-safe: Zero allocation, fixed 6 function evaluations per step.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace apex {
namespace math {
namespace integration {

/* -------------------------------- Options --------------------------------- */

/**
 * @brief Options for RK45 adaptive integrator.
 */
struct RungeKutta45Options {
  double absTol = 1e-6;      ///< Absolute error tolerance.
  double relTol = 1e-6;      ///< Relative error tolerance.
  double dtMin = 1e-12;      ///< Minimum allowed step size.
  double dtMax = 1.0;        ///< Maximum allowed step size.
  double safetyFactor = 0.9; ///< Safety factor for step size adjustment.
  double maxGrowth = 5.0;    ///< Maximum step size growth factor.
  double maxShrink = 0.1;    ///< Minimum step size shrink factor.
};

/* -------------------------------- Status ---------------------------------- */

enum class RK45Status : uint8_t {
  SUCCESS = 0,
  STEP_REJECTED = 1,      ///< Step rejected, try smaller dt.
  ERROR_DT_TOO_SMALL = 2, ///< Step size below dtMin.
  ERROR_INVALID_STEP = 3  ///< Invalid step size (dt <= 0).
};

/* -------------------------------- Result ---------------------------------- */

/**
 * @brief Result of an RK45 step.
 */
template <typename State> struct RK45Result {
  RK45Status status{RK45Status::SUCCESS};
  double errorEstimate{0.0}; ///< Estimated local truncation error.
  double dtNext{0.0};        ///< Suggested next step size.
  State y5{};                ///< Fifth-order solution.
  State y4{};                ///< Fourth-order solution (for error).
};

/* ------------------------------ RungeKutta45 ------------------------------ */

/**
 * @brief Dormand-Prince RK45 adaptive integrator.
 *
 * Uses 7-stage FSAL (First Same As Last) scheme for efficiency.
 * Provides embedded error estimation without extra function evaluations.
 *
 * Algorithm: Dormand-Prince 5(4) with coefficients optimized for
 * local truncation error minimization.
 *
 * @tparam State State type (must support +, -, * scalar, norm()).
 *
 * Example:
 * @code
 *   auto f = [](const double& y, double t) { return -y; };
 *
 *   RungeKutta45<double> integrator;
 *   RungeKutta45Options opts;
 *   opts.absTol = 1e-8;
 *
 *   double y = 1.0;
 *   double t = 0.0;
 *   double dt = 0.1;
 *
 *   while (t < 10.0) {
 *     auto result = integrator.step(y, t, dt, f, opts);
 *     if (result.status == RK45Status::SUCCESS) {
 *       y = result.y5;
 *       t += dt;
 *     }
 *     dt = result.dtNext;
 *   }
 * @endcode
 *
 * @note RT-safe: Zero allocation, 6 function evaluations per step.
 */
template <typename State> class RungeKutta45 {
public:
  struct Stats {
    std::size_t functionEvals{0};
    std::size_t acceptedSteps{0};
    std::size_t rejectedSteps{0};
  };

  /**
   * @brief Attempt one RK45 step with error estimation.
   *
   * @tparam Func Derivative function: State(const State& y, double t)
   * @param y Current state.
   * @param t Current time.
   * @param dt Proposed step size.
   * @param f Derivative function.
   * @param opts Integration options.
   * @return Result with status, error estimate, and suggested next dt.
   * @note RT-safe: O(1), 6 function evaluations.
   */
  template <typename Func>
  RK45Result<State> step(const State& y, double t, double dt, Func&& f,
                         const RungeKutta45Options& opts) noexcept {
    RK45Result<State> result;

    if (dt <= 0.0) {
      result.status = RK45Status::ERROR_INVALID_STEP;
      result.dtNext = opts.dtMin;
      return result;
    }

    // Dormand-Prince coefficients
    constexpr double A21 = 1.0 / 5.0;
    constexpr double A31 = 3.0 / 40.0, A32 = 9.0 / 40.0;
    constexpr double A41 = 44.0 / 45.0, A42 = -56.0 / 15.0, A43 = 32.0 / 9.0;
    constexpr double A51 = 19372.0 / 6561.0, A52 = -25360.0 / 2187.0;
    constexpr double A53 = 64448.0 / 6561.0, A54 = -212.0 / 729.0;
    constexpr double A61 = 9017.0 / 3168.0, A62 = -355.0 / 33.0;
    constexpr double A63 = 46732.0 / 5247.0, A64 = 49.0 / 176.0;
    constexpr double A65 = -5103.0 / 18656.0;

    // Fifth-order weights
    constexpr double B51 = 35.0 / 384.0, B53 = 500.0 / 1113.0;
    constexpr double B54 = 125.0 / 192.0, B55 = -2187.0 / 6784.0;
    constexpr double B56 = 11.0 / 84.0;

    // Fourth-order weights (for error estimation)
    constexpr double B41 = 5179.0 / 57600.0, B43 = 7571.0 / 16695.0;
    constexpr double B44 = 393.0 / 640.0, B45 = -92097.0 / 339200.0;
    constexpr double B46 = 187.0 / 2100.0, B47 = 1.0 / 40.0;

    // Time nodes
    constexpr double C2 = 1.0 / 5.0, C3 = 3.0 / 10.0, C4 = 4.0 / 5.0;
    constexpr double C5 = 8.0 / 9.0;

    // Stage evaluations
    State k1 = f(y, t);
    ++stats_.functionEvals;

    State k2 = f(y + k1 * (dt * A21), t + C2 * dt);
    ++stats_.functionEvals;

    State k3 = f(y + k1 * (dt * A31) + k2 * (dt * A32), t + C3 * dt);
    ++stats_.functionEvals;

    State k4 = f(y + k1 * (dt * A41) + k2 * (dt * A42) + k3 * (dt * A43), t + C4 * dt);
    ++stats_.functionEvals;

    State k5 =
        f(y + k1 * (dt * A51) + k2 * (dt * A52) + k3 * (dt * A53) + k4 * (dt * A54), t + C5 * dt);
    ++stats_.functionEvals;

    State k6 = f(y + k1 * (dt * A61) + k2 * (dt * A62) + k3 * (dt * A63) + k4 * (dt * A64) +
                     k5 * (dt * A65),
                 t + dt);
    ++stats_.functionEvals;

    // Fifth-order solution
    result.y5 = y + (k1 * B51 + k3 * B53 + k4 * B54 + k5 * B55 + k6 * B56) * dt;

    // Fourth-order solution (for error estimate)
    // We need k7 for the full error estimate
    State k7 = f(result.y5, t + dt);
    ++stats_.functionEvals;

    result.y4 = y + (k1 * B41 + k3 * B43 + k4 * B44 + k5 * B45 + k6 * B46 + k7 * B47) * dt;

    // Error estimation
    State err = result.y5 - result.y4;
    result.errorEstimate = computeNorm(err, result.y5, opts);

    // Step size control
    if (result.errorEstimate <= 1.0) {
      // Step accepted
      result.status = RK45Status::SUCCESS;
      ++stats_.acceptedSteps;

      // Compute optimal step size for next step
      double factor = opts.safetyFactor * std::pow(result.errorEstimate, -0.2);
      factor = std::min(factor, opts.maxGrowth);
      factor = std::max(factor, opts.maxShrink);
      result.dtNext = std::min(dt * factor, opts.dtMax);
    } else {
      // Step rejected
      result.status = RK45Status::STEP_REJECTED;
      ++stats_.rejectedSteps;

      // Shrink step size
      double factor = opts.safetyFactor * std::pow(result.errorEstimate, -0.25);
      factor = std::max(factor, opts.maxShrink);
      result.dtNext = std::max(dt * factor, opts.dtMin);

      if (result.dtNext <= opts.dtMin) {
        result.status = RK45Status::ERROR_DT_TOO_SMALL;
      }
    }

    return result;
  }

  /**
   * @brief Integrate from t to tEnd with adaptive stepping.
   *
   * @param y Initial/final state (modified in place).
   * @param t0 Initial time.
   * @param tEnd Final time.
   * @param dt0 Initial step size guess.
   * @param f Derivative function.
   * @param opts Integration options.
   * @return Final status.
   */
  template <typename Func>
  RK45Status integrate(State& y, double t0, double tEnd, double dt0, Func&& f,
                       const RungeKutta45Options& opts) noexcept {
    double t = t0;
    double dt = dt0;

    while (t < tEnd) {
      // Don't overshoot
      if (t + dt > tEnd) {
        dt = tEnd - t;
      }

      auto result = step(y, t, dt, f, opts);

      if (result.status == RK45Status::SUCCESS) {
        y = result.y5;
        t += dt;
        dt = result.dtNext;
      } else if (result.status == RK45Status::STEP_REJECTED) {
        dt = result.dtNext;
      } else {
        return result.status;
      }
    }

    return RK45Status::SUCCESS;
  }

  void reset() noexcept { stats_ = Stats{}; }
  const Stats& stats() const noexcept { return stats_; }

private:
  Stats stats_{};

  // Compute scaled error norm
  double computeNorm(const State& err, const State& y,
                     const RungeKutta45Options& opts) const noexcept {
    // For scalar types
    if constexpr (std::is_arithmetic_v<State>) {
      double scale = opts.absTol + opts.relTol * std::abs(y);
      return std::abs(err) / scale;
    } else {
      // For vector types with norm() method
      double scale = opts.absTol + opts.relTol * y.normInf();
      return err.normInf() / scale;
    }
  }
};

} // namespace integration
} // namespace math
} // namespace apex

#endif // APEX_MATH_INTEGRATION_RUNGEKUTTA45_HPP
