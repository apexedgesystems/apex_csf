#ifndef APEX_MATH_INTEGRATION_BACKWARDEULER_HPP
#define APEX_MATH_INTEGRATION_BACKWARDEULER_HPP
/**
 * @file BackwardEuler.hpp
 * @brief Backward (implicit) Euler integrator.
 *
 * A-stable first-order implicit method for stiff systems.
 * @note RT-safe with RT-safe callbacks (ImplicitOptionsRT).
 */

#include "src/utilities/math/integration/inc/Integration.hpp"
#include "src/utilities/math/integration/inc/IntegrationOptions.hpp"

#include <functional>

namespace apex {
namespace math {
namespace integration {

/* ----------------------------- BackwardEuler ------------------------------ */

/**
 * @brief Implicit (backward) Euler integrator.
 *
 * Solve for x_{n+1} in:
 *   x_{n+1} = x_n + dt * f(x_{n+1}, t_{n+1})
 *
 * Requires a nonlinear solve at each step.
 *
 * @tparam State   Must support +=, -, * double.
 * @tparam Options Must be BackwardEulerOptions<State>.
 */
template <typename State, typename Options = BackwardEulerOptions<State>>
class BackwardEuler : public IntegratorBase<BackwardEuler<State, Options>, State, Options> {
public:
  using Base = IntegratorBase<BackwardEuler, State, Options>;
  using Func = std::function<State(const State&, double)>;

  BackwardEuler() = default;
  ~BackwardEuler() = default;

  /**
   * @brief Bind derivative functor and set initial state/time.
   */
  uint8_t initializeImpl(Func f, const State& initialState, double t0,
                         const Options& opts) noexcept;

  /**
   * @brief One implicit Euler step via Newton (or user solver).
   */
  uint8_t stepImpl(State& state, double t, double dt, const Options& opts) noexcept;

private:
  Func f_;
  State lastState_;
};

} // namespace integration
} // namespace math
} // namespace apex

#include "src/utilities/math/integration/src/BackwardEuler.tpp"
#endif // APEX_MATH_INTEGRATION_BACKWARDEULER_HPP
