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
 *   x_{n+2} = (4/3)*x_{n+1} - (1/3)*x_n + (2/3)*dt*f(x_{n+2}, t_{n+2})
 *
 * Requires solving a nonlinear equation each step.
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
   * @brief Bind derivative functor and set initial time/state history.
   */
  uint8_t initializeImpl(Func f, const State& initialState, double t0,
                         const Options& opts) noexcept;

  /**
   * @brief One BDF2 step via Newton-Raphson.
   *
   * Uses two-step multi-step formula with an implicit solve
   * for x_{n+2} at t_{n+2} = t + dt.
   */
  uint8_t stepImpl(State& state, double t, double dt, const Options& opts) noexcept;

private:
  Func f_;
  State xNm1_;
  State xNm2_;
};

} // namespace integration
} // namespace math
} // namespace apex

#include "src/utilities/math/integration/src/BDF2.tpp"
#endif // APEX_MATH_INTEGRATION_BDF2_HPP
