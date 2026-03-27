#ifndef APEX_MATH_INTEGRATION_TRAPEZOIDALRULE_HPP
#define APEX_MATH_INTEGRATION_TRAPEZOIDALRULE_HPP
/**
 * @file TrapezoidalRule.hpp
 * @brief Implicit trapezoidal (Crank-Nicolson) integrator.
 *
 * A-stable second-order implicit method. Energy-conserving for Hamiltonian systems.
 * @note RT-safe with RT-safe callbacks (ImplicitOptionsRT).
 */

#include "src/utilities/math/integration/inc/Integration.hpp"
#include "src/utilities/math/integration/inc/IntegrationOptions.hpp"

#include <functional>

namespace apex {
namespace math {
namespace integration {

/* ---------------------------- TrapezoidalRule ----------------------------- */

/**
 * @brief Implicit trapezoidal (Crank-Nicolson) integrator.
 *
 *   x_{n+1} = x_n + (dt/2) [ f(x_n, t_n) + f(x_{n+1}, t_{n+1}) ]
 *
 * Requires solving a nonlinear equation each step.
 *
 * @tparam State   Must support +=, -, * double.
 * @tparam Options Must be TrapezoidalRuleOptions<State>.
 */
template <typename State, typename Options = TrapezoidalRuleOptions<State>>
class TrapezoidalRule : public IntegratorBase<TrapezoidalRule<State, Options>, State, Options> {
public:
  using Base = IntegratorBase<TrapezoidalRule<State, Options>, State, Options>;
  using Func = std::function<State(const State&, double)>;

  TrapezoidalRule() = default;
  ~TrapezoidalRule() = default;

  /**
   * @brief Bind derivative functor and set initial time.
   */
  uint8_t initializeImpl(Func f, const State& initialState, double t0,
                         const Options& opts) noexcept;

  /**
   * @brief One Crank-Nicolson step via Newton-Raphson.
   */
  uint8_t stepImpl(State& state, double t, double dt, const Options& opts) noexcept;

private:
  Func f_;
  State savedF_;
};

} // namespace integration
} // namespace math
} // namespace apex

#include "src/utilities/math/integration/src/TrapezoidalRule.tpp"
#endif // APEX_MATH_INTEGRATION_TRAPEZOIDALRULE_HPP
