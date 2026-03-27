/**
 * @file BackwardEuler.tpp
 * @brief Implementation of implicit (backward) Euler integrator.
 */
#ifndef APEX_MATH_INTEGRATION_BACKWARDEULER_TPP
#define APEX_MATH_INTEGRATION_BACKWARDEULER_TPP

#include "src/utilities/math/integration/inc/BackwardEuler.hpp"

#include <utility>

namespace apex {
namespace math {
namespace integration {

/* ----------------------- BackwardEuler Implementation --------------------- */

template <typename State, typename Options>
uint8_t BackwardEuler<State, Options>::initializeImpl(Func f, const State& initialState, double t0,
                                                      const Options& /*opts*/) noexcept {
  this->stats_ = Stats{};
  this->t_ = t0;
  lastState_ = initialState;
  f_ = std::move(f);
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename State, typename Options>
uint8_t BackwardEuler<State, Options>::stepImpl(State& state, double t, double dt,
                                                const Options& opts) noexcept {
  State xNew = state;

  for (std::size_t iter = 0; iter < opts.maxIterations; ++iter) {
    State F = xNew - state - (f_(xNew, t + dt) * dt);
    ++this->stats_.functionEvals;

    auto J = opts.computeJacobian(xNew, t + dt);
    ++this->stats_.jacobianEvals;

    State delta = opts.linearSolve(J, F * -1.0);
    ++this->stats_.linearSolveCalls;

    xNew = xNew + delta;

    if (opts.converged(delta, F)) {
      state = xNew;
      return static_cast<uint8_t>(Status::SUCCESS);
    }
  }

  return static_cast<uint8_t>(Status::ERROR_CONVERGENCE);
}

} // namespace integration
} // namespace math
} // namespace apex

#endif // APEX_MATH_INTEGRATION_BACKWARDEULER_TPP
