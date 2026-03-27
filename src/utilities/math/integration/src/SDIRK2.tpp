/**
 * @file SDIRK2.tpp
 * @brief Implementation of 2-stage SDIRK (Diagonally Implicit RK) integrator.
 */
#ifndef APEX_MATH_INTEGRATION_SDIRK2_TPP
#define APEX_MATH_INTEGRATION_SDIRK2_TPP

#include "src/utilities/math/integration/inc/SDIRK2.hpp"

#include <cmath>

namespace apex {
namespace math {
namespace integration {

/* -------------------------- SDIRK2 Implementation ------------------------- */

template <typename State, typename Options>
uint8_t SDIRK2<State, Options>::initializeImpl(Func f, const State& /*initialState*/, double t0,
                                               const Options& /*opts*/) noexcept {
  this->stats_ = Stats{};
  this->t_ = t0;
  f_ = std::move(f);
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename State, typename Options>
uint8_t SDIRK2<State, Options>::stepImpl(State& state, double t, double dt,
                                         const Options& opts) noexcept {
  const double gamma = GAMMA;
  const double c1 = gamma;
  const double c2 = 1.0;
  const double a11 = gamma;
  const double a21 = 1.0 - gamma;
  const double a22 = gamma;
  const double b1 = 1.0 - gamma;
  const double b2 = gamma;

  // Stage 1: solve Y1 = state + dt * a11 * f(Y1, t + c1*dt)
  State Y1 = state;
  for (std::size_t iter = 0; iter < opts.maxIterations; ++iter) {
    State f1 = f_(Y1, t + c1 * dt);
    ++this->stats_.functionEvals;

    State R1 = Y1 - state - f1 * (dt * a11);

    auto J1 = opts.computeJacobian(Y1, t + c1 * dt);
    ++this->stats_.jacobianEvals;

    State delta1 = opts.linearSolve(J1, R1 * -1.0);
    ++this->stats_.linearSolveCalls;

    Y1 += delta1;
    if (opts.converged(delta1, R1)) {
      break;
    }
  }

  // Stage 2: solve Y2 = state + dt*(a21*f1 + a22*f(Y2, t+dt))
  State Y2 = state;
  for (std::size_t iter = 0; iter < opts.maxIterations; ++iter) {
    State f1 = f_(Y1, t + c1 * dt);
    State f2 = f_(Y2, t + c2 * dt);
    this->stats_.functionEvals += 2;

    State R2 = Y2 - state - (f1 * (dt * a21) + f2 * (dt * a22));

    auto J2 = opts.computeJacobian(Y2, t + c2 * dt);
    ++this->stats_.jacobianEvals;

    State delta2 = opts.linearSolve(J2, R2 * -1.0);
    ++this->stats_.linearSolveCalls;

    Y2 += delta2;
    if (opts.converged(delta2, R2)) {
      break;
    }
  }

  // Combine stages: x_{n+1} = state + dt*(b1*f1 + b2*f2)
  State f1 = f_(Y1, t + c1 * dt);
  ++this->stats_.functionEvals;
  State f2 = f_(Y2, t + c2 * dt);
  ++this->stats_.functionEvals;
  state = state + (f1 * (dt * b1) + f2 * (dt * b2));

  return static_cast<uint8_t>(Status::SUCCESS);
}

} // namespace integration
} // namespace math
} // namespace apex

#endif // APEX_MATH_INTEGRATION_SDIRK2_TPP
