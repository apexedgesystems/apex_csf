#ifndef APEX_MATH_INTEGRATION_INTEGRATION_OPTIONS_HPP
#define APEX_MATH_INTEGRATION_INTEGRATION_OPTIONS_HPP
/**
 * @file IntegrationOptions.hpp
 * @brief Options structs for ODE integrators.
 *
 * Provides configuration for explicit and implicit integration methods.
 * Includes RT-safe ImplicitOptionsRT using Delegate callbacks.
 */

#include <cstddef>
#include <functional>

#include "src/utilities/concurrency/inc/Delegate.hpp"

namespace apex {
namespace math {
namespace integration {

/* --------------------------- Explicit Options ----------------------------- */

/**
 * @brief Options for the explicit Euler integrator.
 */
struct EulerOptions {};

/**
 * @brief Options for the classical 4th-order Runge-Kutta integrator.
 */
struct RungeKutta4Options {};

/* --------------------------- Implicit Options ----------------------------- */

/**
 * @brief Shared options for implicit integrators.
 *
 * Templated on State so we can store type-erased callbacks that
 * know how to compute Jacobians, solve linear systems, and test
 * convergence on a State.
 *
 * @note NOT RT-safe due to std::function. For RT-safe contexts, use
 *       ImplicitOptionsRT instead.
 */
template <typename State> struct ImplicitOptions {
  double tolerance = 1e-6;        /**< Convergence tolerance. */
  std::size_t maxIterations = 10; /**< Maximum Newton-Raphson iterations. */

  /** @brief Compute the Jacobian df/dx at (x, t). */
  std::function<State(const State& x, double t)> computeJacobian;

  /** @brief Solve J * delta = rhs for delta. */
  std::function<State(const State& J, const State& rhs)> linearSolve;

  /**
   * @brief Convergence test: return true when delta and/or F are small.
   *
   * Default always returns true (single iteration). Override for
   * proper convergence checking.
   */
  std::function<bool(const State& delta, const State& F)> converged =
      [](const State&, const State&) { return true; };
};

/* ----------------------- RT-Safe Implicit Options ------------------------- */

/**
 * @brief RT-safe implicit options using Delegate instead of std::function.
 *
 * Uses function pointer + context pattern for zero-allocation callbacks.
 * All callbacks receive a void* context as first parameter.
 *
 * Callback signatures:
 *   - computeJacobian: State (*)(void* ctx, const State& x, double t)
 *   - linearSolve: State (*)(void* ctx, const State& J, const State& rhs)
 *   - converged: bool (*)(void* ctx, const State& delta, const State& F)
 *
 * Example:
 * @code
 *   struct SolverContext {
 *     double jacobianScale;
 *   };
 *
 *   State computeJ(void* ctx, const State& x, double t) noexcept {
 *     auto* c = static_cast<SolverContext*>(ctx);
 *     return x * c->jacobianScale;
 *   }
 *
 *   SolverContext ctx{1.0};
 *   ImplicitOptionsRT<State> opts;
 *   opts.jacobianDelegate = {computeJ, &ctx};
 * @endcode
 *
 * @tparam State State type.
 *
 * @note RT-safe: No allocations, function pointer + context pattern.
 */
template <typename State> struct ImplicitOptionsRT {
  double tolerance = 1e-6;        /**< Convergence tolerance. */
  std::size_t maxIterations = 10; /**< Maximum Newton-Raphson iterations. */

  /** @brief Jacobian callback: State(void* ctx, const State& x, double t). */
  concurrency::Delegate<State, const State&, double> jacobianDelegate;

  /** @brief Linear solver: State(void* ctx, const State& J, const State& rhs). */
  concurrency::Delegate<State, const State&, const State&> linearSolveDelegate;

  /** @brief Convergence test: bool(void* ctx, const State& delta, const State& F). */
  concurrency::Delegate<bool, const State&, const State&> convergedDelegate;

  /**
   * @brief Compute Jacobian using delegate.
   * @param x Current state.
   * @param t Current time.
   * @return Jacobian value (or default State if delegate is null).
   */
  State computeJacobian(const State& x, double t) const noexcept { return jacobianDelegate(x, t); }

  /**
   * @brief Solve linear system using delegate.
   * @param J Jacobian matrix.
   * @param rhs Right-hand side.
   * @return Solution delta (or default State if delegate is null).
   */
  State linearSolve(const State& J, const State& rhs) const noexcept {
    return linearSolveDelegate(J, rhs);
  }

  /**
   * @brief Check convergence using delegate.
   * @param delta Iteration update.
   * @param F Residual.
   * @return True if converged (true if delegate is null).
   */
  bool converged(const State& delta, const State& F) const noexcept {
    if (!convergedDelegate)
      return true;
    return convergedDelegate(delta, F);
  }
};

/* --------------------------- Type Aliases --------------------------------- */

/** @brief Options for backward (implicit) Euler integrator. */
template <typename State> using BackwardEulerOptions = ImplicitOptions<State>;

/** @brief Options for implicit trapezoidal-rule integrator. */
template <typename State> using TrapezoidalRuleOptions = ImplicitOptions<State>;

/** @brief Options for 2-step BDF (Gear's method). */
template <typename State> using BDF2Options = ImplicitOptions<State>;

/** @brief Options for Adams-Moulton predictor-corrector. */
template <typename State> using AdamsMoultonOptions = ImplicitOptions<State>;

/** @brief Options for 2-stage SDIRK (Diagonally Implicit RK). */
template <typename State> using SDIRK2Options = ImplicitOptions<State>;

/* ----------------------- RT-Safe Type Aliases ----------------------------- */

/** @brief RT-safe options for backward Euler. */
template <typename State> using BackwardEulerOptionsRT = ImplicitOptionsRT<State>;

/** @brief RT-safe options for trapezoidal rule. */
template <typename State> using TrapezoidalRuleOptionsRT = ImplicitOptionsRT<State>;

/** @brief RT-safe options for BDF2. */
template <typename State> using BDF2OptionsRT = ImplicitOptionsRT<State>;

/** @brief RT-safe options for Adams-Moulton. */
template <typename State> using AdamsMoultonOptionsRT = ImplicitOptionsRT<State>;

/** @brief RT-safe options for SDIRK2. */
template <typename State> using SDIRK2OptionsRT = ImplicitOptionsRT<State>;

/* ------------------------- Adams-Bashforth Options ------------------------ */

/**
 * @brief Options for Adams-Bashforth explicit multi-step integrator.
 *
 * order = number of steps (1..4)
 */
template <typename State> struct AdamsBashforthOptions {
  std::size_t order = 2; /**< Number of steps (1..4). */
};

} // namespace integration
} // namespace math
} // namespace apex

#endif // APEX_MATH_INTEGRATION_INTEGRATION_OPTIONS_HPP
