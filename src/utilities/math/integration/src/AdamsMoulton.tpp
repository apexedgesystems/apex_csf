/**
 * @file AdamsMoulton.tpp
 * @brief Implementation of Adams-Moulton predictor-corrector integrator.
 */
#ifndef APEX_MATH_INTEGRATION_ADAMSMOULTON_TPP
#define APEX_MATH_INTEGRATION_ADAMSMOULTON_TPP

#include "src/utilities/math/integration/inc/AdamsMoulton.hpp"

namespace apex {
namespace math {
namespace integration {

/* ----------------------- AdamsMoulton Implementation ---------------------- */

template <typename State, typename Options>
uint8_t AdamsMoulton<State, Options>::initializeImpl(Func f, const State& /*initialState*/,
                                                     double t0, const Options& /*opts*/) noexcept {
  this->stats_ = Stats{};
  this->t_ = t0;
  f_ = std::move(f);
  haveHistory_ = false;
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename State, typename Options>
uint8_t AdamsMoulton<State, Options>::stepImpl(State& state, double t, double dt,
                                               const Options& opts) noexcept {
  const State fN = f_(state, t);
  ++this->stats_.functionEvals;

  if (!haveHistory_) {
    // Start step: the corrector needs the derivative one step back, and the
    // first step has none. A classical Runge-Kutta step has a local error of
    // O(dt^5), which leaves the method at third order; a start that guessed
    // the missing derivative would hold it to second.
    const State k2 = f_(state + fN * (0.5 * dt), t + 0.5 * dt);
    const State k3 = f_(state + k2 * (0.5 * dt), t + 0.5 * dt);
    const State k4 = f_(state + k3 * dt, t + dt);
    this->stats_.functionEvals += 3;

    state = state + (fN + k2 * 2.0 + k3 * 2.0 + k4) * (dt / 6.0);
    fPrev_ = fN;
    haveHistory_ = true;
    return static_cast<uint8_t>(Status::SUCCESS);
  }

  // Predictor: Adams-Bashforth of order two, the first Newton iterate
  State xNew = state + ((fN * 1.5 - fPrev_ * 0.5) * dt);

  for (std::size_t iter = 0; iter < opts.maxIterations; ++iter) {
    State fNew = f_(xNew, t + dt);
    ++this->stats_.functionEvals;

    State R = xNew - state - ((fNew * 5.0 + fN * 8.0 - fPrev_) * (dt / 12.0));

    auto J = opts.computeJacobian(xNew, t + dt);
    ++this->stats_.jacobianEvals;

    State delta = opts.linearSolve(J, R * -1.0);
    ++this->stats_.linearSolveCalls;

    xNew += delta;
    if (opts.converged(delta, R)) {
      fPrev_ = fN;
      state = xNew;
      return static_cast<uint8_t>(Status::SUCCESS);
    }
  }

  return static_cast<uint8_t>(Status::ERROR_CONVERGENCE);
}

} // namespace integration
} // namespace math
} // namespace apex

#endif // APEX_MATH_INTEGRATION_ADAMSMOULTON_TPP
