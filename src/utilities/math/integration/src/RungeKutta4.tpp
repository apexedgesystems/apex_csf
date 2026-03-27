/**
 * @file RungeKutta4.tpp
 * @brief Implementation of classical 4th-order Runge-Kutta integrator.
 */
#ifndef APEX_MATH_INTEGRATION_RUNGEKUTTA4_TPP
#define APEX_MATH_INTEGRATION_RUNGEKUTTA4_TPP

#include "src/utilities/math/integration/inc/RungeKutta4.hpp"

#include <utility>

namespace apex {
namespace math {
namespace integration {

/* ------------------------ RungeKutta4 Implementation ---------------------- */

template <typename State, typename Options>
uint8_t RungeKutta4<State, Options>::initializeImpl(Func f, const State& initialState, double t0,
                                                    const Options& /*opts*/) noexcept {
  this->stats_ = Stats{};
  this->t_ = t0;
  state_ = initialState;
  f_ = std::move(f);
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename State, typename Options>
uint8_t RungeKutta4<State, Options>::stepImpl(State& state, double t, double dt,
                                              const Options& /*opts*/) noexcept {
  this->stats_.functionEvals += 4;

  State k1 = f_(state, t);
  State k2 = f_(state + k1 * (dt * 0.5), t + dt * 0.5);
  State k3 = f_(state + k2 * (dt * 0.5), t + dt * 0.5);
  State k4 = f_(state + k3 * dt, t + dt);

  state = state + ((k1 + k2 * 2 + k3 * 2 + k4) * (dt / 6.0));
  return static_cast<uint8_t>(Status::SUCCESS);
}

} // namespace integration
} // namespace math
} // namespace apex

#endif // APEX_MATH_INTEGRATION_RUNGEKUTTA4_TPP
