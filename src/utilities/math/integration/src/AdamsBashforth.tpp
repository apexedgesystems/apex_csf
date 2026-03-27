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
  std::size_t k = std::clamp(opts.order, std::size_t{1}, std::size_t{4});
  k = std::min(k, steps_ + 1);

  // Adams-Bashforth coefficients for orders 1..4
  static constexpr double COEFFS[5][4] = {
      {0.0, 0.0, 0.0, 0.0},                                // unused
      {1.0, 0.0, 0.0, 0.0},                                // AB1 (Euler)
      {3.0 / 2.0, -1.0 / 2.0, 0.0, 0.0},                   // AB2
      {23.0 / 12.0, -4.0 / 3.0, 5.0 / 12.0, 0.0},          // AB3
      {55.0 / 24.0, -59.0 / 24.0, 37.0 / 24.0, -3.0 / 8.0} // AB4
  };

  State sum{};
  for (std::size_t i = 0; i < k; ++i) {
    sum += fHist_[i] * COEFFS[k][i];
  }

  State xNew = state + sum * dt;

  for (std::size_t i = std::min<std::size_t>(3, steps_); i > 0; --i) {
    fHist_[i] = fHist_[i - 1];
  }
  State fNew = f_(xNew, t + dt);
  ++this->stats_.functionEvals;
  fHist_[0] = fNew;

  state = xNew;
  ++steps_;
  return static_cast<uint8_t>(Status::SUCCESS);
}

} // namespace integration
} // namespace math
} // namespace apex

#endif // APEX_MATH_INTEGRATION_ADAMSBASHFORTH_TPP
