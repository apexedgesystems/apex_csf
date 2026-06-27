/**
 * @file StabilityDerivativeAero_uTest.cpp
 * @brief Unit tests for stability-derivative aerodynamics.
 *
 * Exercises the generic model against its conventional default derivatives.
 * Closed-form and sign/monotonicity checks hold for any conventionally-signed
 * parameter set; aircraft-specific preset validation lives with the app.
 *
 * Coverage:
 *   1. Below 1 m/s => zero forces/moments (linearization safe-bail)
 *   2. Level flight (alpha small, beta=0): lift matches q*S*CL closed form,
 *      side force vanishes
 *   3. Pure alpha perturbation: Cm_a < 0 (stable pitch) and L grows with alpha
 *   4. Pure beta perturbation: Cn_b > 0 (weathervane stable), Cl_b < 0
 *      (dihedral stable), CY_b < 0 (side force opposes slip)
 *   5-7. Roll/pitch/yaw damping moments oppose their rates (Cl_p, Cm_q, Cn_r < 0)
 *   8. Elevator: positive de => negative pitch moment (nose-down)
 *   9. Aileron: positive da => positive roll moment (right-wing-down)
 *   10. Rudder: positive dr => negative yaw moment
 *   11. Wind->body force transform behavior
 */

#include "src/sim/aerodynamics/inc/StabilityDerivativeAero.hpp"

#include <cmath>

#include <gtest/gtest.h>

using sim::aerodynamics::ControlInputs;
using sim::aerodynamics::dynamicPressureQ;
using sim::aerodynamics::evaluateStabilityDerivative;
using sim::aerodynamics::StabilityDerivativeAeroParams;
using sim::aerodynamics::StabilityDerivativeAeroResult;
using sim::aerodynamics::windToBodyForces;
using sim::dynamics::rigid_body::Vec3;

namespace {

constexpr double kRho_8km = 0.5258;  // USSA76 rho at 8 km
constexpr double kV_cruise = 240.0;  // m/s, ~Mach 0.81 at 8 km
constexpr double kSmallAlpha = 0.04; // rad (~2.3 deg)

} // namespace

/* ----------------------------- Safe-bail at low V ----------------------------- */

/** @test Below the 1 m/s linearization floor, all forces and moments are zero. */
TEST(StabilityDerivativeAeroTest, BailsCleanlyBelowOneMetersPerSecond) {
  const StabilityDerivativeAeroParams p{};
  // 0.5 m/s along body x - below the 1 m/s linearization floor.
  const auto r = evaluateStabilityDerivative(p, Vec3{0.5, 0.0, 0.0}, Vec3{0.0, 0.0, 0.0},
                                             ControlInputs{}, kRho_8km);
  EXPECT_NEAR(r.force_body.x, 0.0, 1e-12);
  EXPECT_NEAR(r.force_body.y, 0.0, 1e-12);
  EXPECT_NEAR(r.force_body.z, 0.0, 1e-12);
  EXPECT_NEAR(r.moment_body.x, 0.0, 1e-12);
  EXPECT_NEAR(r.moment_body.y, 0.0, 1e-12);
  EXPECT_NEAR(r.moment_body.z, 0.0, 1e-12);
}

/* ----------------------------- Level cruise sanity ----------------------------- */

/**
 * @test In small-alpha, zero-beta level cruise the lift matches the closed form
 * L = q*S*CL with CL = CL_0 + CL_a*alpha, and the side force vanishes.
 */
TEST(StabilityDerivativeAeroTest, LevelCruiseLiftMatchesClosedForm) {
  const StabilityDerivativeAeroParams p{};
  // u along body x, no body-y, small w gives small alpha; beta = 0.
  const double V = kV_cruise;
  const double alpha = kSmallAlpha;
  const Vec3 v_body{V * std::cos(alpha), 0.0, V * std::sin(alpha)};

  const auto r =
      evaluateStabilityDerivative(p, v_body, Vec3{0.0, 0.0, 0.0}, ControlInputs{}, kRho_8km);

  EXPECT_NEAR(r.V_m_s, V, 1e-9);
  EXPECT_NEAR(r.alpha_rad, alpha, 1e-9);
  EXPECT_NEAR(r.beta_rad, 0.0, 1e-12);

  // Closed form: CL = CL_0 + CL_a*alpha  (rates, control = 0).
  const double CL_expected = p.CL_0 + p.CL_a * alpha;
  EXPECT_NEAR(r.CL, CL_expected, 1e-12);

  // L = q*S*CL.
  const double q_bar = dynamicPressureQ(kRho_8km, V);
  EXPECT_NEAR(r.L_N, q_bar * p.S_m2 * CL_expected, 1e-6);

  // beta = 0 => no side force.
  EXPECT_NEAR(r.Y_N, 0.0, 1e-9);
  EXPECT_NEAR(r.force_body.y, 0.0, 1e-9);
}

/* ----------------------------- Static stability signs ----------------------------- */

/**
 * @test Positive alpha yields a negative pitching moment (Cm_a < 0,
 * longitudinal static stability) while lift grows with alpha.
 */
TEST(StabilityDerivativeAeroTest, PositiveAlphaProducesNegativePitchMoment) {
  // Cm_a < 0  =>  longitudinal static stability.
  const StabilityDerivativeAeroParams p{};
  const double V = kV_cruise;
  const Vec3 va0{V, 0.0, 0.0};                                 // alpha = 0
  const Vec3 vap{V * std::cos(0.05), 0.0, V * std::sin(0.05)}; // alpha = +0.05 rad

  const auto r0 = evaluateStabilityDerivative(p, va0, Vec3{}, ControlInputs{}, kRho_8km);
  const auto rp = evaluateStabilityDerivative(p, vap, Vec3{}, ControlInputs{}, kRho_8km);

  EXPECT_LT(rp.moment_body.y, r0.moment_body.y); // delta m_pitch < 0 with delta alpha > 0
  EXPECT_GT(rp.L_N, r0.L_N);                     // lift grows with alpha
}

/**
 * @test Positive sideslip yields weathervane stability: Cn_b > 0 (nose turns
 * into the wind), Cl_b < 0 (raises the upwind wing), and CY_b < 0 (side force
 * opposes the slip).
 */
TEST(StabilityDerivativeAeroTest, PositiveBetaProducesWeathervaneStability) {
  // Cn_b > 0 => positive yawing moment with positive sideslip (nose turns
  // into the wind => stable). Cl_b < 0 => negative roll (raises upwind wing).
  // CY_b < 0 => side force opposes the slip.
  const StabilityDerivativeAeroParams p{};
  const double V = kV_cruise;
  const double beta = 0.05; // ~2.9 deg slip
  const Vec3 v_body{V * std::cos(beta), V * std::sin(beta), 0.0};

  const auto r = evaluateStabilityDerivative(p, v_body, Vec3{}, ControlInputs{}, kRho_8km);

  EXPECT_GT(r.moment_body.z, 0.0); // yaw moment positive (nose-right)
  EXPECT_LT(r.moment_body.x, 0.0); // roll moment negative (raises upwind wing)
  EXPECT_LT(r.Y_N, 0.0);           // side force opposes slip
}

/* ----------------------------- Damping signs ----------------------------- */

/**
 * @test Roll, pitch, and yaw rate damping moments each oppose their rate
 * (Cl_p, Cm_q, Cn_r < 0).
 */
TEST(StabilityDerivativeAeroTest, RollPitchYawDampingMomentsOpposeRates) {
  const StabilityDerivativeAeroParams p{};
  const double V = kV_cruise;
  const Vec3 v_body{V, 0.0, 0.0};

  // Pure roll rate +p => Cl_roll has p_hat*Cl_p < 0 contribution
  const auto r_p =
      evaluateStabilityDerivative(p, v_body, Vec3{0.5, 0.0, 0.0}, ControlInputs{}, kRho_8km);
  EXPECT_LT(r_p.moment_body.x, 0.0);

  // Pure pitch rate +q (Cm_0 = 0; only qHat*Cm_q contributes, negative)
  const auto r_q =
      evaluateStabilityDerivative(p, v_body, Vec3{0.0, 0.1, 0.0}, ControlInputs{}, kRho_8km);
  EXPECT_LT(r_q.moment_body.y, 0.0);

  // Pure yaw rate +r
  const auto r_r =
      evaluateStabilityDerivative(p, v_body, Vec3{0.0, 0.0, 0.1}, ControlInputs{}, kRho_8km);
  EXPECT_LT(r_r.moment_body.z, 0.0);
}

/* ----------------------------- Control surfaces ----------------------------- */

/**
 * @test Elevator deflection moves the pitching moment monotonically: positive
 * de gives a more negative (nose-down) moment and also adds lift via CL_de.
 */
TEST(StabilityDerivativeAeroTest, ElevatorMovesPitchMonotonically) {
  // Positive de (TE down) => negative pitching moment.
  const StabilityDerivativeAeroParams p{};
  const Vec3 v_body{kV_cruise, 0.0, 0.0};

  ControlInputs delta_pos{};
  delta_pos.elevator_rad = +0.05;
  ControlInputs delta_neg{};
  delta_neg.elevator_rad = -0.05;

  const auto r0 = evaluateStabilityDerivative(p, v_body, Vec3{}, ControlInputs{}, kRho_8km);
  const auto rp = evaluateStabilityDerivative(p, v_body, Vec3{}, delta_pos, kRho_8km);
  const auto rn = evaluateStabilityDerivative(p, v_body, Vec3{}, delta_neg, kRho_8km);

  EXPECT_LT(rp.moment_body.y, r0.moment_body.y);
  EXPECT_GT(rn.moment_body.y, r0.moment_body.y);
  EXPECT_GT(rp.L_N, r0.L_N); // de also adds lift via CL_de
}

/** @test Positive aileron deflection produces a positive roll moment (right-wing-down). */
TEST(StabilityDerivativeAeroTest, AileronProducesPositiveRollMoment) {
  // Positive da => positive roll moment (right-wing-down).
  const StabilityDerivativeAeroParams p{};
  const Vec3 v_body{kV_cruise, 0.0, 0.0};
  ControlInputs d{};
  d.aileron_rad = 0.05;

  const auto r0 = evaluateStabilityDerivative(p, v_body, Vec3{}, ControlInputs{}, kRho_8km);
  const auto r = evaluateStabilityDerivative(p, v_body, Vec3{}, d, kRho_8km);
  EXPECT_GT(r.moment_body.x, r0.moment_body.x);
}

/** @test Positive rudder deflection (Cn_dr < 0) produces a negative yaw moment (nose left). */
TEST(StabilityDerivativeAeroTest, RudderProducesNegativeYawMoment) {
  // Positive dr (TE-left) => Cn_dr<0 => negative yaw moment (nose left).
  const StabilityDerivativeAeroParams p{};
  const Vec3 v_body{kV_cruise, 0.0, 0.0};
  ControlInputs d{};
  d.rudder_rad = 0.05;

  const auto r0 = evaluateStabilityDerivative(p, v_body, Vec3{}, ControlInputs{}, kRho_8km);
  const auto r = evaluateStabilityDerivative(p, v_body, Vec3{}, d, kRho_8km);
  EXPECT_LT(r.moment_body.z, r0.moment_body.z);
}

/* ----------------------------- Wind->body transform ----------------------------- */

/** @test At alpha=beta=0 the wind->body rotation is identity: F_body = (-D, +Y, -L). */
TEST(WindToBodyForcesTest, IdentityAtAlphaBetaZero) {
  // At alpha=beta=0, the rotation is identity; F_body = (-D, +Y, -L).
  const Vec3 f = windToBodyForces(/*L*/ 100.0, /*D*/ 50.0, /*Y*/ 30.0, /*alpha*/ 0.0, /*beta*/ 0.0);
  EXPECT_NEAR(f.x, -50.0, 1e-12);
  EXPECT_NEAR(f.y, 30.0, 1e-12);
  EXPECT_NEAR(f.z, -100.0, 1e-12);
}

/**
 * @test With positive alpha the lift vector tilts forward in the body frame:
 * Fx_body = -D*cos(alpha) + L*sin(alpha).
 */
TEST(WindToBodyForcesTest, PositiveAlphaTiltsLiftIntoBodyXForward) {
  // With +alpha, the lift vector tilts forward in the body frame:
  // Fx_body = -D*cos a + L*sin a.
  const Vec3 f =
      windToBodyForces(/*L*/ 1000.0, /*D*/ 100.0, /*Y*/ 0.0, /*alpha*/ 0.10, /*beta*/ 0.0);
  const double expected_x = -100.0 * std::cos(0.1) + 1000.0 * std::sin(0.1);
  EXPECT_NEAR(f.x, expected_x, 1e-9);
}
