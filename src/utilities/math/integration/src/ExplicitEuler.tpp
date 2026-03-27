/**
 * @file ExplicitEuler.tpp
 * @brief Implementation of forward (explicit) Euler integrator.
 */
#ifndef APEX_MATH_INTEGRATION_EXPLICITEULER_TPP
#define APEX_MATH_INTEGRATION_EXPLICITEULER_TPP

#include "src/utilities/math/integration/inc/ExplicitEuler.hpp"

#include <utility>

namespace apex {
namespace math {
namespace integration {

/* ----------------------- ExplicitEuler Implementation --------------------- */

template <typename State, typename Options>
template <Derivative<State> Func>
uint8_t ExplicitEuler<State, Options>::initializeImpl(Func&& f, const State& /*initialState*/,
                                                      double t0, const Options& opts) noexcept {
  this->stats_ = Stats{};
  this->t_ = t0;
  this->options_ = opts;
  f_ = std::forward<Func>(f);
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename State, typename Options>
uint8_t ExplicitEuler<State, Options>::stepImpl(State& state, double t, double dt,
                                                const Options& /*opts*/) noexcept {
  ++this->stats_.functionEvals;
  State k1 = f_(state, t);
  state = state + (k1 * dt);
  return static_cast<uint8_t>(Status::SUCCESS);
}

} // namespace integration
} // namespace math
} // namespace apex

#endif // APEX_MATH_INTEGRATION_EXPLICITEULER_TPP
