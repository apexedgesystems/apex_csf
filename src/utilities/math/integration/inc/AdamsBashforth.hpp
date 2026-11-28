#ifndef APEX_MATH_INTEGRATION_ADAMSBASHFORTH_HPP
#define APEX_MATH_INTEGRATION_ADAMSBASHFORTH_HPP
/**
 * @file AdamsBashforth.hpp
 * @brief Explicit Adams-Bashforth multi-step integrator.
 *
 * Efficient explicit method using past derivative history (order 1-4).
 * @note RT-safe: Zero allocation, O(1) per step with RT-safe callbacks.
 */

#include "src/utilities/math/integration/inc/Integration.hpp"
#include "src/utilities/math/integration/inc/IntegrationOptions.hpp"

#include <algorithm>
#include <array>
#include <functional>

namespace apex {
namespace math {
namespace integration {

/* ----------------------------- AdamsBashforth ----------------------------- */

/**
 * @brief Explicit Adams-Bashforth multi-step integrator.
 *
 *   x_{n+1} = x_n + dt * sum_{i=0}^{k-1} b_i * f_{n-i},
 *   where k = opts.order (1..4).
 *
 * The b_i are those for which the formula is exact for polynomials of
 * degree k.
 *
 * One derivative evaluation per step once k derivatives are held. The first
 * k-1 steps are classical Runge-Kutta steps (four evaluations each), which
 * build that history accurately enough for the method to converge at order k.
 *
 * The step size must be the same on every step: the coefficients are those
 * of equally spaced history. The state passed to step() must be the state
 * the previous step returned: the derivative at it is already held.
 *
 * @tparam State   Must support +=, * double, default-constructible.
 * @tparam Options Must be AdamsBashforthOptions<State>.
 */
template <typename State, typename Options = AdamsBashforthOptions<State>>
class AdamsBashforth : public IntegratorBase<AdamsBashforth<State, Options>, State, Options> {
public:
  using Base = IntegratorBase<AdamsBashforth<State, Options>, State, Options>;
  using Func = std::function<State(const State&, double)>;

  AdamsBashforth() = default;
  ~AdamsBashforth() = default;

  /**
   * @brief Bind f(x,t), reset stats/time, seed history.
   */
  uint8_t initializeImpl(Func f, const State& initialState, double t0,
                         const Options& opts) noexcept;

  /**
   * @brief One step of size dt: Adams-Bashforth of order opts.order, or a
   *        Runge-Kutta start step while fewer than that many derivatives are held.
   */
  uint8_t stepImpl(State& state, double t, double dt, const Options& opts) noexcept;

private:
  static constexpr std::size_t MAX_ORDER = 4;

  Func f_;
  std::array<State, MAX_ORDER> fHist_{}; ///< Derivatives, newest first.
  std::size_t steps_ = 0;                ///< Steps taken since initialize.
};

} // namespace integration
} // namespace math
} // namespace apex

#include "src/utilities/math/integration/src/AdamsBashforth.tpp"
#endif // APEX_MATH_INTEGRATION_ADAMSBASHFORTH_HPP
