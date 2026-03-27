#ifndef APEX_MATH_INTEGRATION_EXPLICITEULER_HPP
#define APEX_MATH_INTEGRATION_EXPLICITEULER_HPP
/**
 * @file ExplicitEuler.hpp
 * @brief Forward (explicit) Euler integrator.
 *
 * First-order explicit method. Simple, fast, suitable for non-stiff systems.
 * @note RT-safe: Zero allocation, O(1) per step.
 */

#include "src/utilities/math/integration/inc/Integration.hpp"
#include "src/utilities/math/integration/inc/IntegrationOptions.hpp"

#include <concepts>
#include <functional>

namespace apex {
namespace math {
namespace integration {

/* ----------------------------- ExplicitEuler ------------------------------ */

/**
 * @brief Forward (explicit) Euler integrator.
 *
 * Zero-allocation, fixed-step method:
 *   x_{n+1} = x_n + dt * f(x_n, t_n)
 *
 * @tparam State   User-defined state (e.g. std::array<double,N>).
 * @tparam Options Algorithm options (defaults to EulerOptions).
 */
template <typename State, typename Options = EulerOptions>
class ExplicitEuler : public IntegratorBase<ExplicitEuler<State, Options>, State, Options> {
public:
  using Base = IntegratorBase<ExplicitEuler, State, Options>;

  ExplicitEuler() = default;
  ~ExplicitEuler() = default;

  /**
   * @brief Reset stats/time/options and bind derivative f(x,t).
   *
   * @tparam Func Callable satisfying Derivative<Func,State>.
   * @param f            f(x,t) -> dx/dt
   * @param initialState Initial state (ignored; user owns state).
   * @param t0           Starting time.
   * @param opts         EulerOptions (stored, not used).
   * @return             uint8_t status code (0 on success).
   */
  template <Derivative<State> Func>
  uint8_t initializeImpl(Func&& f, const State& initialState, double t0,
                         const Options& opts) noexcept;

  /**
   * @brief Perform one forward-Euler step.
   *
   * state <- state + dt * f(state, t)
   *
   * @param state On entry: x_n; on exit: x_{n+1}.
   * @param t     Current time t_n.
   * @param dt    Step size (must be > 0).
   * @param opts  EulerOptions (ignored).
   * @return      uint8_t status code (0 on success).
   */
  uint8_t stepImpl(State& state, double t, double dt, const Options& opts) noexcept;

private:
  std::function<State(const State&, double)> f_;
};

} // namespace integration
} // namespace math
} // namespace apex

#include "src/utilities/math/integration/src/ExplicitEuler.tpp"
#endif // APEX_MATH_INTEGRATION_EXPLICITEULER_HPP
