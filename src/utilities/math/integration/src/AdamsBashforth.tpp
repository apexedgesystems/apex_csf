/**
 * @file AdamsBashforth.tpp
 * @brief Implementation of Adams-Bashforth multi-step integrator.
 */
#ifndef APEX_MATH_INTEGRATION_ADAMSBASHFORTH_TPP
#define APEX_MATH_INTEGRATION_ADAMSBASHFORTH_TPP

#include "src/utilities/math/integration/inc/AdamsBashforth.hpp"

namespace apex {
namespace math {
namespace integration {

/* ---------------------- AdamsBashforth Implementation --------------------- */

template <typename State, typename Options>
uint8_t AdamsBashforth<State, Options>::initializeImpl(Func f, const State& initialState, double t0,
                                                       const Options& /*opts*/) noexcept {
  this->stats_ = Stats{};
  this->t_ = t0;
  f_ = std::move(f);

  State f0 = f_(initialState, t0);
  ++this->stats_.functionEvals;
  fHist_.fill(State{});
  fHist_[0] = f0;
  steps_ = 0;
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename State, typename Options>
uint8_t AdamsBashforth<State, Options>::stepImpl(State& state, double t, double dt,
                                                 const Options& opts) noexcept {
  const std::size_t order = std::clamp(opts.order, std::size_t{1}, MAX_ORDER);

  // Adams-Bashforth coefficients for orders 1..4, newest derivative first
  static constexpr std::array<std::array<double, MAX_ORDER>, MAX_ORDER + 1> COEFFS = {{
      {0.0, 0.0, 0.0, 0.0},                                // unused
      {1.0, 0.0, 0.0, 0.0},                                // AB1 (Euler)
      {3.0 / 2.0, -1.0 / 2.0, 0.0, 0.0},                   // AB2
      {23.0 / 12.0, -4.0 / 3.0, 5.0 / 12.0, 0.0},          // AB3
      {55.0 / 24.0, -59.0 / 24.0, 37.0 / 24.0, -3.0 / 8.0} // AB4
  }};

  // The order-k formula needs k derivatives. After `steps_` steps the history
  // holds steps_ + 1 of them, the newest being the derivative at `state`.
  State xNew{};
  if (steps_ + 1 < order) {
    // Start step: classical Runge-Kutta. Its local error is O(dt^5), so the
    // history it builds does not hold the multistep formula below its order,
    // which a start on the lower-order formulas would.
    const State& k1 = fHist_[0];
    const State k2 = f_(state + k1 * (0.5 * dt), t + 0.5 * dt);
    const State k3 = f_(state + k2 * (0.5 * dt), t + 0.5 * dt);
    const State k4 = f_(state + k3 * dt, t + dt);
    this->stats_.functionEvals += 3;
    xNew = state + (k1 + k2 * 2.0 + k3 * 2.0 + k4) * (dt / 6.0);
  } else {
    State sum{};
    for (std::size_t i = 0; i < order; ++i) {
      sum += fHist_[i] * COEFFS[order][i];
    }
    xNew = state + sum * dt;
  }

  // Age every held derivative by one step, then store the newest
  for (std::size_t i = MAX_ORDER - 1; i > 0; --i) {
    fHist_[i] = fHist_[i - 1];
  }
  fHist_[0] = f_(xNew, t + dt);
  ++this->stats_.functionEvals;

  state = xNew;
  ++steps_;
  return static_cast<uint8_t>(Status::SUCCESS);
}

} // namespace integration
} // namespace math
} // namespace apex

#endif // APEX_MATH_INTEGRATION_ADAMSBASHFORTH_TPP
