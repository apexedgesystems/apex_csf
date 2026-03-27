#ifndef APEX_MATH_INTEGRATION_RUNGEKUTTA4_HPP
#define APEX_MATH_INTEGRATION_RUNGEKUTTA4_HPP
/**
 * @file RungeKutta4.hpp
 * @brief Classical 4th-order Runge-Kutta integrator.
 *
 * Gold standard explicit method. Four function evaluations per step.
 * @note RT-safe: Zero allocation, O(1) per step.
 */

#include "src/utilities/math/integration/inc/Integration.hpp"
#include "src/utilities/math/integration/inc/IntegrationOptions.hpp"

#include <functional>

namespace apex {
namespace math {
namespace integration {

/* ------------------------------ RungeKutta4 ------------------------------- */

/**
 * @brief Classical 4th-order Runge-Kutta integrator.
 *
 * Zero-allocation, fixed-step:
 *   k1 = f(x, t)
 *   k2 = f(x + 0.5*dt*k1, t + 0.5*dt)
 *   k3 = f(x + 0.5*dt*k2, t + 0.5*dt)
 *   k4 = f(x + dt*k3, t + dt)
 *   x_{n+1} = x_n + (dt/6)*(k1 + 2*k2 + 2*k3 + k4)
 *
 * @tparam State   User-defined state (+=, * double).
 * @tparam Options Algorithm options (defaults to RungeKutta4Options).
 */
template <typename State, typename Options = RungeKutta4Options>
class RungeKutta4 : public IntegratorBase<RungeKutta4<State, Options>, State, Options> {
public:
  using Base = IntegratorBase<RungeKutta4, State, Options>;
  using Func = std::function<State(const State&, double)>;

  RungeKutta4() = default;
  ~RungeKutta4() = default;

  /**
   * @brief Reset stats/time/options and bind derivative f(x,t).
   *
   * @param f            f(x,t) -> dx/dt
   * @param initialState Initial state (ignored; user owns state).
   * @param t0           Starting time.
   * @param opts         RungeKutta4Options (ignored).
   * @return             uint8_t status code (0 on success).
   */
  uint8_t initializeImpl(Func f, const State& initialState, double t0,
                         const Options& opts) noexcept;

  /**
   * @brief Perform one RK4 step.
   *
   * state <- state + (dt/6)*(k1 + 2*k2 + 2*k3 + k4)
   *
   * @param state On entry: x_n; on exit: x_{n+1}.
   * @param t     Current time t_n.
   * @param dt    Step size (must be > 0).
   * @param opts  RungeKutta4Options (ignored).
   * @return      uint8_t status code (0 on success).
   */
  uint8_t stepImpl(State& state, double t, double dt, const Options& opts) noexcept;

private:
  Func f_;
  State state_;
};

} // namespace integration
} // namespace math
} // namespace apex

#include "src/utilities/math/integration/src/RungeKutta4.tpp"
#endif // APEX_MATH_INTEGRATION_RUNGEKUTTA4_HPP
