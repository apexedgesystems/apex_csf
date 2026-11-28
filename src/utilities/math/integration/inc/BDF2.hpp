#ifndef APEX_MATH_INTEGRATION_BDF2_HPP
#define APEX_MATH_INTEGRATION_BDF2_HPP
/**
 * @file BDF2.hpp
 * @brief 2-step BDF (Gear's second-order backward differentiation) integrator.
 *
 * A-stable second-order implicit method for stiff systems.
 * @note RT-safe with RT-safe callbacks (ImplicitOptionsRT).
 */

#include "src/utilities/math/integration/inc/Integration.hpp"
#include "src/utilities/math/integration/inc/IntegrationOptions.hpp"

#include <functional>

namespace apex {
namespace math {
namespace integration {

/* ---------------------------------- BDF2 ---------------------------------- */

/**
 * @brief 2-step BDF (Gear's second-order backward differentiation) integrator.
 *
 *   x_{n+1} = (4/3)*x_n - (1/3)*x_{n-1} + (2/3)*dt*f(x_{n+1}, t_{n+1})
 *
 * the backward differentiation formula of order 2.
 *
 * Requires solving a nonlinear equation each step. x_n is the state passed
 * to step(); x_{n-1} is the state passed to the step before it. The first
 * step after initialize has no x_{n-1} and uses the one-step rule
 *
 *   x_1 = x_0 + dt*((1/3)*f(x_0, t_0) + (2/3)*f(x_1, t_1)),
 *
 * which is stable for every step size on a decaying problem and keeps the
 * method second order. Both formulas have the Newton matrix
 * I - (2/3)*dt*df/dx: that is what opts.computeJacobian returns on every
 * step, the first included.
 *
 * The step size must be the same on every step: the coefficients are those
 * of equally spaced history. A small correction to the state between steps
 * is stepped from as given. After a jump in the state or a change of step
 * size, call initialize again, so that the next step is a start step and
 * not a two-step formula over history that no longer fits.
 *
 * @tparam State   Must support +=, -, * double.
 * @tparam Options Must be BDF2Options<State>.
 */
template <typename State, typename Options = BDF2Options<State>>
class BDF2 : public IntegratorBase<BDF2<State, Options>, State, Options> {
public:
  using Base = IntegratorBase<BDF2<State, Options>, State, Options>;
  using Func = std::function<State(const State&, double)>;

  BDF2() = default;
  ~BDF2() = default;

  /**
   * @brief Bind the derivative functor and set the initial time. The first
   *        step starts from the state passed to it.
   */
  uint8_t initializeImpl(Func f, const State& initialState, double t0,
                         const Options& opts) noexcept;

  /**
   * @brief One step via Newton-Raphson, from `state` to t + dt.
   *
   * The two-step formula once a previous state is held, the one-step start
   * rule before that. On ERROR_CONVERGENCE the state and the history are
   * unchanged.
   */
  uint8_t stepImpl(State& state, double t, double dt, const Options& opts) noexcept;

private:
  Func f_;
  State xPrev_{};            ///< State one step back, x_{n-1}.
  bool haveHistory_ = false; ///< Whether a step has been taken since initialize.
};

} // namespace integration
} // namespace math
} // namespace apex

#include "src/utilities/math/integration/src/BDF2.tpp"
#endif // APEX_MATH_INTEGRATION_BDF2_HPP
