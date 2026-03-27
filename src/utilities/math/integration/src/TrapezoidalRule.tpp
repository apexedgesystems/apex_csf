/**
 * @file TrapezoidalRule.tpp
 * @brief Implementation of implicit trapezoidal (Crank-Nicolson) integrator.
 */
#ifndef APEX_MATH_INTEGRATION_TRAPEZOIDALRULE_TPP
#define APEX_MATH_INTEGRATION_TRAPEZOIDALRULE_TPP

#include "src/utilities/math/integration/inc/TrapezoidalRule.hpp"

namespace apex {
namespace math {
namespace integration {

/* --------------------- TrapezoidalRule Implementation --------------------- */

template <typename State, typename Options>
uint8_t TrapezoidalRule<State, Options>::initializeImpl(Func f, const State& /*initialState*/,
                                                        double t0,
                                                        const Options& /*opts*/) noexcept {
  this->stats_ = Stats{};
  this->t_ = t0;
  f_ = std::move(f);
  savedF_ = State{};
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename State, typename Options>
uint8_t TrapezoidalRule<State, Options>::stepImpl(State& state, double t, double dt,
                                                  const Options& opts) noexcept {
  savedF_ = f_(state, t);
  ++this->stats_.functionEvals;

  State xNew = state;

  for (std::size_t iter = 0; iter < opts.maxIterations; ++iter) {
    State fNew = f_(xNew, t + dt);
    ++this->stats_.functionEvals;

    State R = xNew - state - (savedF_ + fNew) * (0.5 * dt);

    auto J = opts.computeJacobian(xNew, t + dt);
    ++this->stats_.jacobianEvals;

    State delta = opts.linearSolve(J, R * -1.0);
    ++this->stats_.linearSolveCalls;

    xNew += delta;

    if (opts.converged(delta, R)) {
      state = xNew;
      return static_cast<uint8_t>(Status::SUCCESS);
    }
  }

  return static_cast<uint8_t>(Status::ERROR_CONVERGENCE);
}

} // namespace integration
} // namespace math
} // namespace apex

#endif // APEX_MATH_INTEGRATION_TRAPEZOIDALRULE_TPP
