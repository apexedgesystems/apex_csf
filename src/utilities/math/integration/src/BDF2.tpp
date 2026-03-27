/**
 * @file BDF2.tpp
 * @brief Implementation of 2-step BDF (Gear's method) integrator.
 */
#ifndef APEX_MATH_INTEGRATION_BDF2_TPP
#define APEX_MATH_INTEGRATION_BDF2_TPP

#include "src/utilities/math/integration/inc/BDF2.hpp"

namespace apex {
namespace math {
namespace integration {

/* --------------------------- BDF2 Implementation ------------------------- */

template <typename State, typename Options>
uint8_t BDF2<State, Options>::initializeImpl(Func f, const State& initialState, double t0,
                                             const Options& /*opts*/) noexcept {
  this->stats_ = Stats{};
  this->t_ = t0;
  f_ = std::move(f);

  xNm2_ = initialState;
  xNm1_ = initialState;
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename State, typename Options>
uint8_t BDF2<State, Options>::stepImpl(State& state, double t, double dt,
                                       const Options& opts) noexcept {
  State xNew = xNm1_;

  for (std::size_t iter = 0; iter < opts.maxIterations; ++iter) {
    State fx = f_(xNew, t + dt);
    ++this->stats_.functionEvals;

    State R = xNew - ((xNm1_ * (4.0 / 3.0)) - (xNm2_ * (1.0 / 3.0))) - (fx * ((2.0 / 3.0) * dt));

    auto J = opts.computeJacobian(xNew, t + dt);
    ++this->stats_.jacobianEvals;

    State delta = opts.linearSolve(J, R * -1.0);
    ++this->stats_.linearSolveCalls;

    xNew += delta;

    if (opts.converged(delta, R)) {
      xNm2_ = xNm1_;
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

#endif // APEX_MATH_INTEGRATION_BDF2_TPP
