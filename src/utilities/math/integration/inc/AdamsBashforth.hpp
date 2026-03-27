#ifndef APEX_MATH_INTEGRATION_ADAMSBASHFORTH_HPP
#define APEX_MATH_INTEGRATION_ADAMSBASHFORTH_HPP
/**
 * @file AdamsBashforth.hpp
 * @brief Explicit Adams-Bashforth multi-step integrator.
 *
 * Efficient explicit method using past derivative history (order 1-4).
 * @note RT-safe: Zero allocation, O(1) per step.
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
 * Uses lower-order formula for the first few steps.
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
   * @brief One AB step of size dt.
   * Uses up to opts.order previous f-values.
   */
  uint8_t stepImpl(State& state, double t, double dt, const Options& opts) noexcept;

private:
  Func f_;
  std::array<State, 4> fHist_{};
  std::size_t steps_ = 0;
};

} // namespace integration
} // namespace math
} // namespace apex

#include "src/utilities/math/integration/src/AdamsBashforth.tpp"
#endif // APEX_MATH_INTEGRATION_ADAMSBASHFORTH_HPP
