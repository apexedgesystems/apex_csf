#ifndef APEX_SIM_DYNAMICS_INTEGRATORS_RK4_HPP
#define APEX_SIM_DYNAMICS_INTEGRATORS_RK4_HPP
/**
 * @file RK4.hpp
 * @brief Classical fourth-order Runge-Kutta ODE integrator.
 *
 * Single-step explicit method, 4 derivative evaluations per step:
 *
 *   k1 = f(t,           y)
 *   k2 = f(t + dt/2,    y + (dt/2) * k1)
 *   k3 = f(t + dt/2,    y + (dt/2) * k2)
 *   k4 = f(t + dt,      y +  dt    * k3)
 *
 *   y_{n+1} = y_n + (dt / 6) * (k1 + 2*k2 + 2*k3 + k4)
 *
 * Default integrator for atmospheric flight: handles 6DOF rigid-body
 * dynamics with quaternion attitude at typical ~100 Hz tick rates with
 * good accuracy.
 *
 * Templated on a `State` type that supports `operator+` (state + state)
 * and `operator*` (state * scalar).
 */

namespace sim::dynamics::integrators {

template <typename State, typename Derivative>
inline void stepRK4(State& state, Derivative&& derivative, double t, double dt) {
  const double half = 0.5 * dt;
  const State k1 = derivative(t, state);
  const State k2 = derivative(t + half, state + k1 * half);
  const State k3 = derivative(t + half, state + k2 * half);
  const State k4 = derivative(t + dt, state + k3 * dt);
  state = state + (k1 + k2 * 2.0 + k3 * 2.0 + k4) * (dt / 6.0);
}

} // namespace sim::dynamics::integrators

#endif // APEX_SIM_DYNAMICS_INTEGRATORS_RK4_HPP
