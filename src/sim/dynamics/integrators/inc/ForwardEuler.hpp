#ifndef APEX_SIM_DYNAMICS_INTEGRATORS_FORWARD_EULER_HPP
#define APEX_SIM_DYNAMICS_INTEGRATORS_FORWARD_EULER_HPP
/**
 * @file ForwardEuler.hpp
 * @brief First-order explicit Euler ODE integrator.
 *
 * Single-step explicit method:
 *
 *   y_{n+1} = y_n + dt * f(t_n, y_n)
 *
 * Cheap and stable for slow-dynamics systems like cruise-flight kinematics.
 * Use `RK4` when accuracy matters (anything with appreciable rotation rates,
 * orbital propagation, or stiff dynamics).
 *
 * Templated on a `State` type that supports `operator+` (state + state) and
 * `operator*` (state * scalar). Concrete State types live with each
 * rigid-body / vehicle model -- e.g. PointMass3D::State.
 */

namespace sim::dynamics::integrators {

/**
 * Advance `state` by one Euler step of size `dt` using force/moment
 * function `derivative`. The derivative function takes (t, state) and
 * returns dstate/dt of the same `State` type.
 *
 * @param  state       current state, mutated in place
 * @param  derivative  function (t, state) -> dstate/dt
 * @param  t           current time
 * @param  dt          step size
 */
template <typename State, typename Derivative>
inline void stepForwardEuler(State& state, Derivative&& derivative, double t, double dt) {
  const State k1 = derivative(t, state);
  state = state + k1 * dt;
}

} // namespace sim::dynamics::integrators

#endif // APEX_SIM_DYNAMICS_INTEGRATORS_FORWARD_EULER_HPP
