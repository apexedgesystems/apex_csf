/**
 * @file TransportPhugoidSim_uTest.cpp
 * @brief Closed-loop physics verification — phugoid (long-period) mode.
 *
 * Couples RigidBody6DOF + StabilityDerivativeAero (the transport cruise
 * preset), perturbs the trimmed aircraft with a small airspeed excess,
 * and measures the slow altitude/airspeed exchange — the phugoid. With
 * the short period fast and well damped (Cm_a, Cm_q), angle of attack
 * stays pinned near trim and the residual motion is a nearly-lossless
 * trade between kinetic and potential energy at constant thrust.
 *
 * Expected period, derived from that energy exchange: with alpha (and
 * so CL) held at trim, a speed excess dV raises lift by 2*W*dV/V,
 * giving vertical acceleration g*2*dV/V — a harmonic oscillator with
 *
 *   w_n = g * sqrt(2) / V ~= 9.807 * 1.414 / 235.9 ~= 0.0588 rad/s
 *   T   = 2*pi / w_n ~= 107 s
 *
 * Pitch-attitude freedom and drag variation shift the real mode some
 * tens of seconds either way; we pin the period in [80, 140] s — wide
 * enough for that spread, tight enough to fail on a 2x error. Damping
 * is light (set by drag-to-lift ratio, ~0.07 here): we assert the
 * oscillation does not grow.
 *
 * If this test fails, points to:
 *   - Lift/drag trim balance (CL_0, CD_0 + induced at the reference
 *     condition; the preset trims level at 236 m/s by construction)
 *   - Gravity rotation into the body frame
 *   - Energy conservation in the 6DOF translational EOM
 */

#include "src/sim/aerodynamics/inc/StabilityDerivativeAero.hpp"
#include "src/sim/dynamics/rigid_body/inc/RigidBody6DOF.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <gtest/gtest.h>

using sim::aerodynamics::ControlInputs;
using sim::aerodynamics::evaluateStabilityDerivative;
using sim::aerodynamics::transportCruisePreset;
using sim::dynamics::rigid_body::InertiaTensor;
using sim::dynamics::rigid_body::RigidBody6DOFState;
using sim::dynamics::rigid_body::stepRigidBody6DOF;
using sim::dynamics::rigid_body::Vec3;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kG = 9.80665;

// Same cruise reference condition + mass properties as the Dutch roll
// suite; the preset's lift at CL_0 balances this weight at 235.9 m/s.
constexpr double kV_cruise = 235.9;
constexpr double kRho_cruise = 0.3045;
constexpr double kMass_kg = 288800.0;
constexpr double kIxx = 24675886.0;
constexpr double kIyy = 44877562.0;
constexpr double kIzz = 67384138.0;
constexpr double kIxz = 1315143.0;

} // namespace

TEST(TransportPhugoidSim, SpeedAltitudeExchangePeriodMatchesEnergyModel) {
  const auto aero = transportCruisePreset();
  const InertiaTensor I{kIxx, kIyy, kIzz, kIxz};

  // ---- Trim with a small airspeed excess (phugoid-selective) ----
  // A pure speed perturbation barely excites the (fast, damped) short
  // period, so the response is dominated by the long-period exchange.
  const double dV = 10.0;
  RigidBody6DOFState s;
  s.position_inertial = Vec3{0.0, 0.0, -12192.0};
  s.velocity_body = Vec3{kV_cruise + dV, 0.0, 0.0};
  s.attitude = apex::math::integration::Quaternion{1.0, 0.0, 0.0, 0.0};
  s.angular_velocity_body = Vec3{0.0, 0.0, 0.0};

  // Thrust frozen at the TRIM value (constant-thrust phugoid): drag at
  // the trim point, not at the perturbed speed.
  const double q_bar_trim = 0.5 * kRho_cruise * kV_cruise * kV_cruise;
  const double CL_trim = aero.CL_0;
  const double CD_trim = aero.CD_0 + (CL_trim * CL_trim) / (kPi * aero.e_oswald * aero.AR);
  const double trim_thrust_N = q_bar_trim * aero.S_m2 * CD_trim;

  const double dt = 0.01;
  const double t_end = 260.0; // ~2.4 expected periods
  const int n_steps = static_cast<int>(t_end / dt);

  std::vector<double> t_hist, du_hist;
  t_hist.reserve(n_steps);
  du_hist.reserve(n_steps);

  auto force_fn = [&](double /*t*/, const RigidBody6DOFState& st) {
    ControlInputs delta; // stick fixed: all surfaces at trim
    const auto a = evaluateStabilityDerivative(aero, st.velocity_body, st.angular_velocity_body,
                                               delta, kRho_cruise);
    const auto q_conj = st.attitude.conjugate();
    const auto g_body = q_conj.rotate(0.0, 0.0, +kMass_kg * kG);
    return Vec3{a.force_body.x + g_body[0] + trim_thrust_N, a.force_body.y + g_body[1],
                a.force_body.z + g_body[2]};
  };

  auto moment_fn = [&](double /*t*/, const RigidBody6DOFState& st) {
    ControlInputs delta;
    const auto a = evaluateStabilityDerivative(aero, st.velocity_body, st.angular_velocity_body,
                                               delta, kRho_cruise);
    return a.moment_body;
  };

  const bool debug = std::getenv("APEX_PHUGOID_DEBUG") != nullptr;

  for (int i = 0; i < n_steps; ++i) {
    const double t = i * dt;
    stepRigidBody6DOF(s, force_fn, moment_fn, kMass_kg, I, t, dt);
    t_hist.push_back((i + 1) * dt);
    du_hist.push_back(s.velocity_body.x - kV_cruise);

    if (debug && (i % 500 == 0)) {
      std::fprintf(stderr, "t=%-8.1f du=%-10.3f alt=%-10.1f\n", (i + 1) * dt,
                   s.velocity_body.x - kV_cruise, -s.position_inertial.z);
    }
  }

  // ---- Period from airspeed-excess zero crossings ----
  // Skip the first few seconds so short-period residue settles.
  std::vector<double> zero_crossings;
  for (size_t i = 1; i < t_hist.size(); ++i) {
    if (t_hist[i] < 5.0)
      continue;
    if ((du_hist[i - 1] < 0.0 && du_hist[i] >= 0.0) ||
        (du_hist[i - 1] > 0.0 && du_hist[i] <= 0.0)) {
      const double frac = -du_hist[i - 1] / (du_hist[i] - du_hist[i - 1]);
      zero_crossings.push_back(t_hist[i - 1] + frac * dt);
    }
  }

  ASSERT_GE(zero_crossings.size(), 2u)
      << "Need >=2 airspeed-excess sign changes; got " << zero_crossings.size();

  const double T_phugoid = 2.0 * (zero_crossings[1] - zero_crossings[0]);
  if (debug)
    std::fprintf(stderr, "T (phugoid) = %.1f s\n", T_phugoid);

  EXPECT_GT(T_phugoid, 80.0) << "T = " << T_phugoid << " s (expected ~107 s)";
  EXPECT_LT(T_phugoid, 140.0) << "T = " << T_phugoid << " s (expected ~107 s)";

  // Light positive damping: the second airspeed peak must not exceed
  // the first (drag bleeds energy every cycle).
  double peak1 = 0.0, peak2 = 0.0;
  for (size_t i = 0; i < t_hist.size(); ++i) {
    const double a = std::fabs(du_hist[i]);
    if (t_hist[i] < T_phugoid && a > peak1)
      peak1 = a;
    if (t_hist[i] >= T_phugoid && t_hist[i] < 2.0 * T_phugoid && a > peak2)
      peak2 = a;
  }
  EXPECT_LT(peak2, peak1 * 1.05) << "Phugoid grew: |du| 1st cycle = " << peak1
                                 << ", 2nd cycle = " << peak2;
}
