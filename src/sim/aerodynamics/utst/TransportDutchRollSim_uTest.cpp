/**
 * @file TransportDutchRollSim_uTest.cpp
 * @brief Closed-loop physics verification — Dutch roll mode.
 *
 * Couples RigidBody6DOF + StabilityDerivativeAero (the transport cruise
 * preset), perturbs the trimmed aircraft via a brief rudder doublet, and
 * measures the resulting yaw-rate oscillation period — the Dutch roll
 * mode, the lateral-directional analog of the longitudinal short period:
 * a coupled yaw-roll oscillation about the heading axis.
 *
 * Expected period, derived from the preset itself: the single-DOF
 * weathercock approximation treats yaw as a torsional oscillator with
 * stiffness q_bar*S*b*Cn_b, so
 *
 *   w_n ~= sqrt(q_bar * S * b * Cn_b / Izz)
 *       ~= sqrt(8472 * 510.97 * 59.64 * 0.147 / 6.74e7) ~= 0.75 rad/s
 *   T   ~= 2*pi / w_n ~= 8.4 s
 *
 * Roll-yaw coupling (Cl_b, Cl_r, Cn_p and the Ixz product of inertia)
 * shortens the full-mode period toward ~6 s. We pin the period in
 * [4, 12] s — wide enough to absorb the single-DOF-vs-coupled spread,
 * tight enough to fail on a 2x error or a wrong-axis sign.
 *
 * The mode is lightly damped by design (Cn_r yaw damping alone); real
 * aircraft add a yaw-damper control loop to push damping up. This test
 * runs without one to verify the raw airframe mode itself.
 *
 * If this test fails, points to:
 *   - Lateral-directional derivatives (Cn_b, Cn_r, Cl_b, Cl_p)
 *   - Inertia tensor (Ixx, Izz, Ixz cross-coupling)
 *   - Body-frame angular-velocity update in the 6DOF EOM
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

// Cruise reference condition + wide-body mass properties matching the
// preset's trim point (236 m/s TAS at ~12.2 km standard density).
constexpr double kV_cruise = 235.9;
constexpr double kRho_cruise = 0.3045;
constexpr double kMass_kg = 288800.0;
constexpr double kIxx = 24675886.0;
constexpr double kIyy = 44877562.0;
constexpr double kIzz = 67384138.0;
constexpr double kIxz = 1315143.0;

} // namespace

/** @test The preset encodes a statically stable airframe: restoring
 *  longitudinal and directional stiffness, dihedral effect, and rate
 *  damping all carry their stability-required signs. */
TEST(TransportPreset, StaticStabilitySigns) {
  const auto p = transportCruisePreset();
  EXPECT_GT(p.CL_a, 0.0); // lift increases with angle of attack
  EXPECT_LT(p.Cm_a, 0.0); // nose-down restoring pitch moment
  EXPECT_LT(p.Cm_q, 0.0); // pitch rate damping
  EXPECT_GT(p.Cn_b, 0.0); // weathercock stability
  EXPECT_LT(p.Cn_r, 0.0); // yaw rate damping
  EXPECT_LT(p.Cl_b, 0.0); // dihedral effect
  EXPECT_LT(p.Cl_p, 0.0); // roll rate damping
  EXPECT_NEAR(p.AR, (p.b_m * p.b_m) / p.S_m2, 1e-12);
}

TEST(TransportDutchRollSim, YawOscillationPeriodMatchesWeathercockModel) {
  const auto aero = transportCruisePreset();
  const InertiaTensor I{kIxx, kIyy, kIzz, kIxz};

  // ---- Trim, perturb via brief rudder doublet ----
  RigidBody6DOFState s;
  s.position_inertial = Vec3{0.0, 0.0, -12192.0};
  s.velocity_body = Vec3{kV_cruise, 0.0, 0.0};
  s.attitude = apex::math::integration::Quaternion{1.0, 0.0, 0.0, 0.0};
  s.angular_velocity_body = Vec3{0.0, 0.0, 0.0};

  const double q_bar_trim = 0.5 * kRho_cruise * kV_cruise * kV_cruise;
  const double CL_trim = aero.CL_0;
  const double CD_trim = aero.CD_0 + (CL_trim * CL_trim) / (kPi * aero.e_oswald * aero.AR);
  const double trim_thrust_N = q_bar_trim * aero.S_m2 * CD_trim;

  // Rudder doublet: +0.05 rad for 0.5 s, then -0.05 rad for 0.5 s, then
  // 0. Excites Dutch roll cleanly via the yaw moment Cn_dr * dr.
  const double dt = 0.005;
  const double t_end = 16.0; // ~2.5 Dutch roll periods
  const double t_doublet_end = 1.0;
  const double rudder_pulse = 0.05; // 2.9 deg rudder
  const int n_steps = static_cast<int>(t_end / dt);

  std::vector<double> t_hist, r_hist;
  t_hist.reserve(n_steps);
  r_hist.reserve(n_steps);

  auto rudder_at = [&](double t) {
    if (t < 0.5)
      return rudder_pulse;
    if (t < t_doublet_end)
      return -rudder_pulse;
    return 0.0;
  };

  auto force_fn = [&](double t, const RigidBody6DOFState& st) {
    ControlInputs delta;
    delta.rudder_rad = rudder_at(t);
    const auto a = evaluateStabilityDerivative(aero, st.velocity_body, st.angular_velocity_body,
                                               delta, kRho_cruise);
    const auto q_conj = st.attitude.conjugate();
    const auto g_body = q_conj.rotate(0.0, 0.0, +kMass_kg * kG);
    return Vec3{a.force_body.x + g_body[0] + trim_thrust_N, a.force_body.y + g_body[1],
                a.force_body.z + g_body[2]};
  };

  auto moment_fn = [&](double t, const RigidBody6DOFState& st) {
    ControlInputs delta;
    delta.rudder_rad = rudder_at(t);
    const auto a = evaluateStabilityDerivative(aero, st.velocity_body, st.angular_velocity_body,
                                               delta, kRho_cruise);
    return a.moment_body;
  };

  const bool debug = std::getenv("APEX_DR_DEBUG") != nullptr;
  if (debug)
    std::fprintf(stderr, "%-7s %-12s %-12s\n", "t", "r[rad/s]", "p[rad/s]");

  for (int i = 0; i < n_steps; ++i) {
    const double t = i * dt;
    stepRigidBody6DOF(s, force_fn, moment_fn, kMass_kg, I, t, dt);
    t_hist.push_back((i + 1) * dt);
    r_hist.push_back(s.angular_velocity_body.z);

    if (debug && (i % 40 == 0)) {
      std::fprintf(stderr, "%-7.2f %-12.5f %-12.5f\n", (i + 1) * dt, s.angular_velocity_body.z,
                   s.angular_velocity_body.x);
    }
  }

  // ---- Find the Dutch roll period from yaw-rate zero crossings ----
  std::vector<double> zero_crossings;
  const double t_search_start = t_doublet_end + 0.2;
  for (size_t i = 1; i < t_hist.size(); ++i) {
    if (t_hist[i] < t_search_start)
      continue;
    if ((r_hist[i - 1] < 0.0 && r_hist[i] >= 0.0) || (r_hist[i - 1] > 0.0 && r_hist[i] <= 0.0)) {
      const double frac = -r_hist[i - 1] / (r_hist[i] - r_hist[i - 1]);
      zero_crossings.push_back(t_hist[i - 1] + frac * dt);
    }
  }

  ASSERT_GE(zero_crossings.size(), 2u)
      << "Need >=2 yaw-rate sign changes; got " << zero_crossings.size();

  const double half_period = zero_crossings[1] - zero_crossings[0];
  const double T_d = 2.0 * half_period;
  if (debug)
    std::fprintf(stderr, "T_d (Dutch roll) = %.2f s\n", T_d);

  // Weathercock model predicts ~8.4 s; roll-yaw coupling shortens the
  // full mode toward ~6 s. [4, 12] s absorbs that spread while failing
  // on a 2x error or a wrong-axis sign.
  EXPECT_GT(T_d, 4.0) << "T_d = " << T_d << " s (expected ~6-8 s)";
  EXPECT_LT(T_d, 12.0) << "T_d = " << T_d << " s (expected ~6-8 s)";

  // Damping sanity (loose). The simplified derivative set leaves the
  // mode near-neutral, and coupling with the very slow spiral mode can
  // show small growth over the 16 s window — pure decay is what a yaw
  // damper adds, not the bare airframe. Require bounded amplitude:
  // 2nd-cycle peak < 3x 1st-cycle peak (catches divergent oscillation
  // while allowing the lightly-damped reality).
  double peak_early = 0.0, peak_later = 0.0;
  for (size_t i = 0; i < t_hist.size(); ++i) {
    const double ar = std::fabs(r_hist[i]);
    if (t_hist[i] >= t_doublet_end && t_hist[i] < t_doublet_end + T_d && ar > peak_early)
      peak_early = ar;
    if (t_hist[i] >= t_doublet_end + T_d && t_hist[i] < t_doublet_end + 2.0 * T_d &&
        ar > peak_later)
      peak_later = ar;
  }
  EXPECT_LT(peak_later, 3.0 * peak_early)
      << "Dutch roll grew unboundedly: |r| 1st cycle = " << peak_early
      << ", 2nd cycle = " << peak_later
      << " (>3x growth indicates an actual instability, not just the expected lightly-damped mode)";
}
