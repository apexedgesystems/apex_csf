#ifndef APEX_MATH_INTEGRATION_ADAMSMOULTON_HPP
#define APEX_MATH_INTEGRATION_ADAMSMOULTON_HPP
/**
 * @file AdamsMoulton.hpp
 * @brief 2-step Adams-Moulton predictor-corrector (implicit multi-step).
 *
 * High-accuracy implicit method using Adams-Bashforth predictor.
 * @note RT-safe with RT-safe callbacks (ImplicitOptionsRT).
 */

#include "src/utilities/math/integration/inc/Integration.hpp"
#include "src/utilities/math/integration/inc/IntegrationOptions.hpp"

#include <functional>

namespace apex {
namespace math {
namespace integration {

/* ----------------------------- AdamsMoulton ------------------------------- */

/**
 * @brief 2-step Adams-Moulton predictor-corrector (implicit multi-step).
 *
 * Predictor:  x_pred = x_n + dt*(1.5 f_n - 0.5 f_{n-1})
 * Corrector: solve
 *   x_{n+1} = x_n + (dt/12)[5 f(x_{n+1},t_{n+1}) + 8 f_n - f_{n-1}]
 *
 * Third order. x_n is the state passed to step() and f_n the derivative
 * there; f_{n-1} is the derivative at the state passed to the step before.
 * The first step after initialize has no f_{n-1}: it is one classical
 * Runge-Kutta step, explicit, which makes no solve and does not call the
 * solver callbacks. On the corrector steps opts.computeJacobian returns the
 * Newton matrix I - (5/12)*dt*df/dx.
 *
 * The step size must be the same on every step: the coefficients are those
 * of equally spaced history. After a jump in the state or a change of step
 * size, call initialize again.
 *
 * @tparam State   Must support +=, -, * double.
 * @tparam Options Must be AdamsMoultonOptions<State>.
 */
template <typename State, typename Options = AdamsMoultonOptions<State>>
class AdamsMoulton : public IntegratorBase<AdamsMoulton<State, Options>, State, Options> {
public:
  using Base = IntegratorBase<AdamsMoulton<State, Options>, State, Options>;
  using Func = std::function<State(const State&, double)>;

  AdamsMoulton() = default;
  ~AdamsMoulton() = default;

  /**
   * @brief Bind the derivative functor and set the initial time. The first
   *        step starts from the state passed to it.
   */
  uint8_t initializeImpl(Func f, const State& initialState, double t0,
                         const Options& opts) noexcept;

  /**
   * @brief One step of size dt: predictor and Newton-Raphson corrector, or
   *        the Runge-Kutta start step on the first call after initialize.
   *        On ERROR_CONVERGENCE the state and the history are unchanged.
   */
  uint8_t stepImpl(State& state, double t, double dt, const Options& opts) noexcept;

private:
  Func f_;
  State fPrev_{};            ///< Derivative one step back, f_{n-1}.
  bool haveHistory_ = false; ///< Whether a step has been taken since initialize.
};

} // namespace integration
} // namespace math
} // namespace apex

#include "src/utilities/math/integration/src/AdamsMoulton.tpp"
#endif // APEX_MATH_INTEGRATION_ADAMSMOULTON_HPP
