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
uint8_t AdamsMoulton<State, Options>::initializeImpl(Func f, const State& initialState, double t0,
                                                     const Options& /*opts*/) noexcept {
  this->stats_ = Stats{};
  this->t_ = t0;
  f_ = std::move(f);

  xNm2_ = initialState;
  xNm1_ = initialState;
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename State, typename Options>
uint8_t AdamsMoulton<State, Options>::stepImpl(State& state, double t, double dt,
                                               const Options& opts) noexcept {
  State fN = f_(state, t);
  ++this->stats_.functionEvals;
  State fNm1 = f_(xNm2_, t - dt);
  ++this->stats_.functionEvals;

  State xPred = state + ((fN * 1.5 - fNm1 * 0.5) * dt);

  State xNew = xPred;

  for (std::size_t iter = 0; iter < opts.maxIterations; ++iter) {
    State fNew = f_(xNew, t + dt);
    ++this->stats_.functionEvals;

    State R = xNew - state - ((fNew * 5.0 + fN * 8.0 - fNm1) * (dt / 12.0));

    auto J = opts.computeJacobian(xNew, t + dt);
    ++this->stats_.jacobianEvals;

    State delta = opts.linearSolve(J, R * -1.0);
    ++this->stats_.linearSolveCalls;

    xNew += delta;
    if (opts.converged(delta, R)) {
      xNm2_ = state;
      xNm1_ = xNew;
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
