#ifndef APEX_MATH_INTEGRATION_INTEGRATION_HPP
#define APEX_MATH_INTEGRATION_INTEGRATION_HPP
/**
 * @file Integration.hpp
 * @brief CRTP base class and common types for ODE integrators.
 *
 * Design goals:
 *  - Zero-allocation stepping (all state in templates)
 *  - RT-safe API (no heap, bounded execution)
 *  - Compile-time polymorphism via CRTP
 */

#include "src/utilities/math/integration/inc/IntegrationOptions.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace apex {
namespace math {
namespace integration {

/* -------------------------------- Concepts -------------------------------- */

/**
 * @brief Concept for a derivative function f(x, t) -> dx/dt.
 *
 * Ensures the callable can be invoked as Func(const State&, double)
 * and returns a State.
 */
template <typename Func, typename State>
concept Derivative = std::invocable<Func, const State&, double> &&
                     std::same_as<std::invoke_result_t<Func, const State&, double>, State>;

/* --------------------------------- Status --------------------------------- */

/**
 * @brief Error and status codes used by all integrators.
 */
enum class Status : uint8_t {
  SUCCESS = 0,                /**< Completed successfully. */
  ERROR_INVALID_STEP = 1,     /**< Provided time-step <= 0. */
  ERROR_MAX_ITERATIONS = 2,   /**< Implicit solver exceeded max iterations. */
  ERROR_JACOBIAN_FAILURE = 3, /**< Jacobian evaluation failed or returned NaN. */
  ERROR_LINEAR_SOLVER = 4,    /**< Linear solver reported failure. */
  ERROR_CONVERGENCE = 5,      /**< Implicit method did not converge. */
  ERROR_UNKNOWN = 255         /**< Unspecified error. */
};

/* ---------------------------------- Stats --------------------------------- */

/**
 * @brief Statistics collected during integration.
 *
 * Counts function evaluations, Jacobian evaluations,
 * linear-solver calls, and step rejections.
 */
struct Stats {
  std::size_t functionEvals = 0;    /**< Number of f(x,t) calls. */
  std::size_t jacobianEvals = 0;    /**< Number of Jacobian evaluations. */
  std::size_t linearSolveCalls = 0; /**< Number of linear-solve invocations. */
  std::size_t stepRejections = 0;   /**< Number of rejected steps (adaptive). */
};

/* ------------------------------ IntegratorBase ---------------------------- */

/**
 * @brief CRTP base for all ODE integrators.
 *
 * Provides a uniform initialize/step interface, time bookkeeping,
 * and shared stats collection without virtual calls or heap use.
 *
 * Derived classes must implement:
 *   - uint8_t initializeImpl(Func&&, const State&, double t0, const Options&) noexcept;
 *   - uint8_t stepImpl(State& state, double t, double dt, const Options&) noexcept;
 *
 * @tparam Derived  The concrete integrator inheriting this base.
 * @tparam State    User-defined state container (e.g., std::array<double,N>).
 * @tparam Options  Algorithm-specific options struct.
 */
template <typename Derived, typename State, typename Options> class IntegratorBase {
public:
  /**
   * @brief Initialize the integrator with derivative function and initial conditions.
   *
   * @tparam Func Callable satisfying Derivative<Func,State>.
   * @param f            Derivative function f(x,t) -> dx/dt.
   * @param initialState Initial state vector.
   * @param t0           Starting time.
   * @param opts         Algorithm-specific options.
   * @return             Status code (0 on success).
   */
  template <Derivative<State> Func>
  uint8_t initialize(Func&& f, const State& initialState, double t0, const Options& opts) noexcept {
    stats_ = Stats{};
    t_ = t0;
    options_ = opts;
    return static_cast<uint8_t>(
        static_cast<Derived*>(this)->initializeImpl(std::forward<Func>(f), initialState, t0, opts));
  }

  /**
   * @brief Advance the state by one time step.
   *
   * @param state Current state (modified in-place).
   * @param dt    Time step size (must be > 0).
   * @param opts  Algorithm-specific options.
   * @return      Status code (0 on success).
   */
  uint8_t step(State& state, double dt, const Options& opts) noexcept {
    if (dt <= 0.0) {
      ++stats_.stepRejections;
      return static_cast<uint8_t>(Status::ERROR_INVALID_STEP);
    }

    uint8_t s = static_cast<Derived*>(this)->stepImpl(state, t_, dt, opts);

    if (s == static_cast<uint8_t>(Status::SUCCESS)) {
      t_ += dt;
      options_ = opts;
    }

    return s;
  }

  /** @brief Retrieve integration statistics. */
  const Stats& stats() const noexcept { return stats_; }

  /** @brief Current simulation time. */
  double time() const noexcept { return t_; }

protected:
  Stats stats_{};
  double t_{};
  Options options_{};
};

/* ----------------------- Forward Declarations ----------------------------- */

/** @name Explicit Steppers
 *  @{
 */

/**
 * @brief Forward Euler integrator.
 *
 * Path: src/utilities/math/integration/src/ExplicitEuler.tpp
 */
template <typename State, typename Options> class ExplicitEuler;

/**
 * @brief Classical 4th-order Runge-Kutta integrator.
 *
 * Path: src/utilities/math/integration/src/RungeKutta4.tpp
 */
template <typename State, typename Options> class RungeKutta4;

/** @} */

/** @name Implicit Steppers
 *  @{
 */

/**
 * @brief Implicit (Backward) Euler integrator.
 *
 * Path: src/utilities/math/integration/src/BackwardEuler.tpp
 */
template <typename State, typename Options> class BackwardEuler;

/**
 * @brief Implicit trapezoidal-rule integrator.
 *
 * Path: src/utilities/math/integration/src/TrapezoidalRule.tpp
 */
template <typename State, typename Options> class TrapezoidalRule;

/**
 * @brief 2-step BDF (Gear's second-order backward differentiation).
 *
 * Path: src/utilities/math/integration/src/BDF2.tpp
 */
template <typename State, typename Options> class BDF2;

/**
 * @brief Adams-Moulton predictor-corrector (implicit multi-step).
 *
 * Path: src/utilities/math/integration/src/AdamsMoulton.tpp
 */
template <typename State, typename Options> class AdamsMoulton;

/**
 * @brief Adams-Bashforth explicit multi-step (1 f-call per step).
 *
 * Path: src/utilities/math/integration/src/AdamsBashforth.tpp
 */
template <typename State, typename Options> class AdamsBashforth;

/**
 * @brief 2-stage SDIRK (Diagonally Implicit RK).
 *
 * Path: src/utilities/math/integration/src/SDIRK2.tpp
 */
template <typename State, typename Options> class SDIRK2;

/** @} */

} // namespace integration
} // namespace math
} // namespace apex

#endif // APEX_MATH_INTEGRATION_INTEGRATION_HPP
