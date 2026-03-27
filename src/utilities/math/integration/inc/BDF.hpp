#ifndef APEX_MATH_INTEGRATION_BDF_HPP
#define APEX_MATH_INTEGRATION_BDF_HPP
/**
 * @file BDF.hpp
 * @brief Backward Differentiation Formulas (BDF1-BDF6) for stiff systems.
 *
 * Multi-step implicit methods with increasing order and stability.
 * Industry standard for stiff ODEs in chemical, thermal, and control systems.
 *
 * Stability: BDF1-2 are A-stable, BDF3-6 are stiffly stable.
 * @note RT-safe with RT-safe callbacks.
 */

#include "src/utilities/math/integration/inc/Integration.hpp"
#include "src/utilities/math/integration/inc/IntegrationOptions.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace apex {
namespace math {
namespace integration {

/* ---------------------------------- BDF ----------------------------------- */

/**
 * @brief Variable-order BDF integrator (order 1-6).
 *
 * Uses backward difference formulas with Newton iteration for implicit solve.
 * Stores history of past states for multi-step methods.
 *
 * BDF formulas:
 *   BDF1: y_{n+1} - y_n = dt * f(y_{n+1})
 *   BDF2: 3/2*y_{n+1} - 2*y_n + 1/2*y_{n-1} = dt * f(y_{n+1})
 *   BDF3-6: Higher-order analogs
 *
 * @tparam State State type.
 * @tparam MaxOrder Maximum order (1-6, default 6).
 * @tparam Options Options type (ImplicitOptions or ImplicitOptionsRT).
 *
 * Example:
 * @code
 *   BDF<double, 4> integrator;  // Up to BDF4
 *   // ... configure options with Jacobian, linear solver, convergence test
 *   integrator.initialize(f, y0, t0);
 *
 *   for (int i = 0; i < 100; ++i) {
 *     integrator.step(y, dt, opts);
 *   }
 * @endcode
 *
 * @note RT-safe with RT-safe callbacks.
 */
template <typename State, std::size_t MaxOrder = 6, typename Options = ImplicitOptions<State>>
class BDF {
  static_assert(MaxOrder >= 1 && MaxOrder <= 6, "BDF order must be 1-6");

public:
  using Func = std::function<State(const State&, double)>;

  struct Stats {
    std::size_t functionEvals{0};
    std::size_t jacobianEvals{0};
    std::size_t linearSolveCalls{0};
    std::size_t steps{0};
  };

  /** @brief Current order being used. */
  std::size_t currentOrder() const noexcept { return order_; }

  /**
   * @brief Initialize the integrator.
   *
   * @tparam F Derivative function type.
   * @param f Derivative function.
   * @param y0 Initial state.
   * @param t0 Initial time.
   */
  template <typename F> void initialize(F&& f, const State& y0, double t0) noexcept {
    f_ = std::forward<F>(f);
    t_ = t0;
    stats_ = Stats{};
    order_ = 1;
    historyCount_ = 0;
    history_[0] = y0;
  }

  /**
   * @brief Perform one BDF step.
   *
   * @param state Current state (modified in place).
   * @param dt Time step.
   * @param opts Implicit solver options.
   * @return Status code.
   */
  uint8_t step(State& state, double dt, const Options& opts) noexcept {
    if (dt <= 0.0) {
      return static_cast<uint8_t>(Status::ERROR_INVALID_STEP);
    }

    // Compute predictor using history
    State xNew = state; // Use current state as initial guess

    // Newton iteration for implicit solve
    const double ALPHA_K = ALPHA[order_];

    for (std::size_t iter = 0; iter < opts.maxIterations; ++iter) {
      State fNew = f_(xNew, t_ + dt);
      ++stats_.functionEvals;

      // Residual: alpha*x_{n+1} - sum(beta_i * x_{n-i}) - dt*f(x_{n+1}) = 0
      State rhs = computeRHS(state);
      State R = xNew * ALPHA_K - rhs - fNew * dt;

      auto J = opts.computeJacobian(xNew, t_ + dt);
      ++stats_.jacobianEvals;

      State delta = opts.linearSolve(J, R * -1.0);
      ++stats_.linearSolveCalls;

      xNew = xNew + delta;

      if (opts.converged(delta, R)) {
        // Update history
        updateHistory(state);
        state = xNew;

        // Ramp up order if we have enough history
        if (order_ < MaxOrder && historyCount_ >= order_) {
          ++order_;
        }

        t_ += dt;
        ++stats_.steps;

        return static_cast<uint8_t>(Status::SUCCESS);
      }
    }

    return static_cast<uint8_t>(Status::ERROR_CONVERGENCE);
  }

  double time() const noexcept { return t_; }
  const Stats& stats() const noexcept { return stats_; }

  void reset(double t0 = 0.0) noexcept {
    t_ = t0;
    stats_ = Stats{};
    order_ = 1;
    historyCount_ = 0;
  }

private:
  // BDF coefficients for each order
  // alpha[k] * y_{n+1} + sum(beta[k][i] * y_{n-i}) = dt * f(y_{n+1})
  static constexpr std::array<double, 7> ALPHA = {
      0.0, 1.0, 3.0 / 2.0, 11.0 / 6.0, 25.0 / 12.0, 137.0 / 60.0, 147.0 / 60.0};

  static constexpr std::array<std::array<double, 6>, 7> BETA = {{
      {{0.0, 0.0, 0.0, 0.0, 0.0, 0.0}},                 // unused
      {{1.0, 0.0, 0.0, 0.0, 0.0, 0.0}},                 // BDF1
      {{2.0, -0.5, 0.0, 0.0, 0.0, 0.0}},                // BDF2
      {{3.0, -1.5, 1.0 / 3.0, 0.0, 0.0, 0.0}},          // BDF3
      {{4.0, -3.0, 4.0 / 3.0, -0.25, 0.0, 0.0}},        // BDF4
      {{5.0, -5.0, 10.0 / 3.0, -1.25, 0.2, 0.0}},       // BDF5
      {{6.0, -7.5, 20.0 / 3.0, -3.75, 1.2, -1.0 / 6.0}} // BDF6
  }};

  // Compute RHS = sum(beta_i * x_{n-i})
  State computeRHS(const State& current) const noexcept {
    State rhs = current * BETA[order_][0];
    for (std::size_t i = 1; i < order_ && i <= historyCount_; ++i) {
      rhs = rhs + history_[i - 1] * BETA[order_][i];
    }
    return rhs;
  }

  void updateHistory(const State& state) noexcept {
    // Shift history
    for (std::size_t i = MaxOrder - 1; i > 0; --i) {
      history_[i] = history_[i - 1];
    }
    history_[0] = state;
    if (historyCount_ < MaxOrder) {
      ++historyCount_;
    }
  }

  Func f_;
  double t_{0.0};
  Stats stats_{};
  std::size_t order_{1};
  std::size_t historyCount_{0};
  std::array<State, MaxOrder> history_{};
};

/* ------------------------------ Type Aliases ------------------------------ */

/** @brief BDF with maximum order 3 (most commonly used for moderate stiffness). */
template <typename State, typename Options = ImplicitOptions<State>>
using BDF3 = BDF<State, 3, Options>;

/** @brief BDF with maximum order 4. */
template <typename State, typename Options = ImplicitOptions<State>>
using BDF4 = BDF<State, 4, Options>;

/** @brief BDF with maximum order 5. */
template <typename State, typename Options = ImplicitOptions<State>>
using BDF5 = BDF<State, 5, Options>;

/** @brief BDF with maximum order 6 (maximum stability). */
template <typename State, typename Options = ImplicitOptions<State>>
using BDF6 = BDF<State, 6, Options>;

} // namespace integration
} // namespace math
} // namespace apex

#endif // APEX_MATH_INTEGRATION_BDF_HPP
