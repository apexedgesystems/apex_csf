/**
 * @file TransportShortPeriodSpiralSim_uTest.cpp
 * @brief Closed-loop physics verification — short period and spiral modes.
 *
 * Completes the dynamic-mode anchor set for the transport preset: with
 * Dutch roll and phugoid pinned by their own suites, these two close
 * the table (every mode set-piece the demo excites has an in-suite
 * closed-form crosscheck).
 *
 * SHORT PERIOD — the fast longitudinal mode: alpha and pitch rate
 * oscillate about the velocity vector while speed barely changes.
 * Treating pitch as a torsional oscillator about the restoring
 * stiffness (the longitudinal analog of the Dutch roll suite's
 * weathercock model):
 *
 *   w_n ~= sqrt(q_bar * S * c * (-Cm_a) / Iyy)
 *       ~= sqrt(8472 * 510.97 * 8.324 * 1.026 / 4.488e7) ~= 0.91 rad/s
 *
 * Damping comes from pitch-rate damping plus the lift-slope path:
 *
 *   2*zeta*w_n ~= (q_bar*S*c^2 / (2*Iyy*V)) * (-Cm_q)
 *              + (q_bar*S / (m*V)) * CL_a          ~= 0.59  =>  zeta ~= 0.32
 *
 * so the damped period is T ~= 2*pi/(w_n*sqrt(1-zeta^2)) ~= 7.3 s and
 * each successive peak carries exp(-zeta*w_n*T) ~= 12% of the last —
 * the "airframe handles this one itself" contrast case among the mode
 * demonstrations. Pinned: period in [4, 11] s, second-cycle peak
 * below half the first (expected ~0.12, wide margin).
 *
 * SPIRAL — the slow lateral mode: a bank disturbance either winds up
 * (unstable) or bleeds off (stable) depending on the competition
 * between dihedral effect and yaw stiffness. The lateral quartic's
 * constant term carries the verdict through the determinant
 *
 *   E ~ Cl_b*Cn_r - Cn_b*Cl_r
 *     = (-0.193)(-0.228) - (0.147)(0.212) = +0.0128
 *
 * Positive, but small against the damping products, so the root sits
 * essentially at the origin: the 6DOF measures |lambda| ~ 3e-5 1/s
 * (time-to-double beyond six sim-hours) — NEAR-NEUTRAL on any demo
 * timescale, the classic transport spiral. The sim half releases the
 * airframe with a bank disturbance and pins exactly that: the bank
 * neither winds up nor bleeds off across 70 s (bounded both ways),
 * which also cleanly separates the mode from the fast roll
 * subsidence. The demo set-piece therefore shows PERSISTENCE — the
 * bank lingers until the loops are re-enabled and the autopilot rolls
 * it away — and its V&V criterion is bank persistence within a band,
 * not a doubling/halving time.
 *
 * If these fail, points to:
 *   - Longitudinal: Cm_a, Cm_q sign/magnitude, Iyy, CL_a path
 *   - Spiral: Cl_b/Cn_r/Cn_b/Cl_r balance, gravity term in the
 *     body-frame force assembly (the spiral is gravity-driven)
 */

#include "src/sim/aerodynamics/inc/StabilityDerivativeAero.hpp"
#include "src/sim/dynamics/rigid_body/inc/RigidBody6DOF.hpp"

#include <cmath>
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
// preset's trim point (as the Dutch roll / phugoid suites).
constexpr double kV_cruise = 235.9;
constexpr double kRho_cruise = 0.3045;
constexpr double kMass_kg = 288800.0;
constexpr double kIxx = 24675886.0;
constexpr double kIyy = 44877562.0;
constexpr double kIzz = 67384138.0;
constexpr double kIxz = 1315143.0;

/// Trimmed initial state at the cruise point.
RigidBody6DOFState cruiseTrimState() {
  RigidBody6DOFState s;
  s.position_inertial = Vec3{0.0, 0.0, -12192.0};
  s.velocity_body = Vec3{kV_cruise, 0.0, 0.0};
  s.attitude = apex::math::integration::Quaternion{1.0, 0.0, 0.0, 0.0};
  s.angular_velocity_body = Vec3{0.0, 0.0, 0.0};
  return s;
}

double trimThrustN(const sim::aerodynamics::StabilityDerivativeAeroParams& aero) {
  const double q_bar = 0.5 * kRho_cruise * kV_cruise * kV_cruise;
  const double CL = aero.CL_0;
  const double CD = aero.CD_0 + (CL * CL) / (kPi * aero.e_oswald * aero.AR);
  return q_bar * aero.S_m2 * CD;
}

} // namespace

TEST(TransportShortPeriodSim, PitchOscillationPeriodAndDampingMatchModel) {
  const auto aero = transportCruisePreset();
  const InertiaTensor I{kIxx, kIyy, kIzz, kIxz};
  auto s = cruiseTrimState();
  const double trim_thrust_N = trimThrustN(aero);

  // Elevator pulse: -0.05 rad for 0.5 s (the EXCITE_MODE id 2 shape).
  const double dt = 0.005;
  const double t_end = 30.0;
  auto elevator_at = [](double t) { return (t < 0.5) ? -0.05 : 0.0; };

  auto force_fn = [&](double t, const RigidBody6DOFState& st) {
    ControlInputs delta;
    delta.elevator_rad = elevator_at(t);
    const auto a = evaluateStabilityDerivative(aero, st.velocity_body, st.angular_velocity_body,
                                               delta, kRho_cruise);
    const auto q_conj = st.attitude.conjugate();
    const auto g_body = q_conj.rotate(0.0, 0.0, +kMass_kg * kG);
    return Vec3{a.force_body.x + g_body[0] + trim_thrust_N, a.force_body.y + g_body[1],
                a.force_body.z + g_body[2]};
  };
  auto moment_fn = [&](double t, const RigidBody6DOFState& st) {
    ControlInputs delta;
    delta.elevator_rad = elevator_at(t);
    const auto a = evaluateStabilityDerivative(aero, st.velocity_body, st.angular_velocity_body,
                                               delta, kRho_cruise);
    return a.moment_body;
  };

  const int n_steps = static_cast<int>(t_end / dt);
  std::vector<double> t_hist, q_hist;
  t_hist.reserve(static_cast<std::size_t>(n_steps));
  q_hist.reserve(static_cast<std::size_t>(n_steps));
  for (int i = 0; i < n_steps; ++i) {
    stepRigidBody6DOF(s, force_fn, moment_fn, kMass_kg, I, i * dt, dt);
    t_hist.push_back((i + 1) * dt);
    q_hist.push_back(s.angular_velocity_body.y);
  }

  // Period from pitch-rate zero crossings after the pulse. The phugoid
  // rides underneath at ~110 s; over the first two short-period cycles
  // its contribution to q is negligible.
  std::vector<double> crossings;
  for (std::size_t i = 1; i < t_hist.size(); ++i) {
    if (t_hist[i] < 0.7) {
      continue;
    }
    if ((q_hist[i - 1] < 0.0 && q_hist[i] >= 0.0) || (q_hist[i - 1] > 0.0 && q_hist[i] <= 0.0)) {
      const double frac = -q_hist[i - 1] / (q_hist[i] - q_hist[i - 1]);
      crossings.push_back(t_hist[i - 1] + frac * dt);
    }
  }
  ASSERT_GE(crossings.size(), 2u) << "need >=2 pitch-rate sign changes";
  const double T_sp = 2.0 * (crossings[1] - crossings[0]);

  // Torsional model predicts 6.9 s undamped, 7.3 s damped; [4, 11]
  // absorbs the model-vs-6DOF spread while failing a 2x error.
  EXPECT_GT(T_sp, 4.0) << "T_sp = " << T_sp;
  EXPECT_LT(T_sp, 11.0) << "T_sp = " << T_sp;

  // Damping: successive-cycle peak ratio ~exp(-zeta*w_n*T) ~= 0.12.
  // Require < 0.5 — the well-damped contrast to the Dutch roll's
  // light damping, with margin for the model-vs-6DOF spread.
  double peak1 = 0.0;
  double peak2 = 0.0;
  for (std::size_t i = 0; i < t_hist.size(); ++i) {
    const double aq = std::fabs(q_hist[i]);
    if (t_hist[i] >= 0.7 && t_hist[i] < 0.7 + T_sp && aq > peak1) {
      peak1 = aq;
    }
    if (t_hist[i] >= 0.7 + T_sp && t_hist[i] < 0.7 + 2.0 * T_sp && aq > peak2) {
      peak2 = aq;
    }
  }
  EXPECT_LT(peak2, 0.5 * peak1) << "short period underdamped: cycle peaks " << peak1 << " -> "
                                << peak2;
}

TEST(TransportSpiralSim, BankDisturbancePersistsOnTheSpiralTimescale) {
  const auto aero = transportCruisePreset();

  // Closed-form half: the lateral quartic's constant-term determinant.
  // Positive (no divergence), and small: the composition pin for the
  // near-neutral root the sim half measures.
  const double E_DET = aero.Cl_b * aero.Cn_r - aero.Cn_b * aero.Cl_r;
  EXPECT_GT(E_DET, 0.0) << "preset spiral determinant flipped sign";

  const InertiaTensor I{kIxx, kIyy, kIzz, kIxz};
  auto s = cruiseTrimState();
  const double trim_thrust_N = trimThrustN(aero);

  // Aileron pulse (EXCITE_MODE id 3 shape) seeds a bank offset, then
  // the airframe flies free for 120 s.
  const double dt = 0.005;
  const double t_end = 120.0;
  auto aileron_at = [](double t) { return (t < 0.5) ? 0.02 : 0.0; };

  auto force_fn = [&](double t, const RigidBody6DOFState& st) {
    ControlInputs delta;
    delta.aileron_rad = aileron_at(t);
    const auto a = evaluateStabilityDerivative(aero, st.velocity_body, st.angular_velocity_body,
                                               delta, kRho_cruise);
    const auto q_conj = st.attitude.conjugate();
    const auto g_body = q_conj.rotate(0.0, 0.0, +kMass_kg * kG);
    return Vec3{a.force_body.x + g_body[0] + trim_thrust_N, a.force_body.y + g_body[1],
                a.force_body.z + g_body[2]};
  };
  auto moment_fn = [&](double t, const RigidBody6DOFState& st) {
    ControlInputs delta;
    delta.aileron_rad = aileron_at(t);
    const auto a = evaluateStabilityDerivative(aero, st.velocity_body, st.angular_velocity_body,
                                               delta, kRho_cruise);
    return a.moment_body;
  };

  // Bank angle from the quaternion each step; average |phi| over a
  // Dutch-roll period (~7 s) around two well-separated sample times so
  // the superimposed oscillation cancels and only the spiral envelope
  // remains.
  const int n_steps = static_cast<int>(t_end / dt);
  std::vector<double> t_hist, phi_hist;
  t_hist.reserve(static_cast<std::size_t>(n_steps));
  phi_hist.reserve(static_cast<std::size_t>(n_steps));
  for (int i = 0; i < n_steps; ++i) {
    stepRigidBody6DOF(s, force_fn, moment_fn, kMass_kg, I, i * dt, dt);
    const auto& q = s.attitude;
    const double phi =
        std::atan2(2.0 * (q.w * q.x + q.y * q.z), 1.0 - 2.0 * (q.x * q.x + q.y * q.y));
    t_hist.push_back((i + 1) * dt);
    phi_hist.push_back(phi);
  }

  auto meanAbsPhi = [&](double t_center) {
    double sum = 0.0;
    int n = 0;
    for (std::size_t i = 0; i < t_hist.size(); ++i) {
      if (std::fabs(t_hist[i] - t_center) <= 3.5) {
        sum += std::fabs(phi_hist[i]);
        ++n;
      }
    }
    return (n > 0) ? sum / n : 0.0;
  };

  const double PHI_EARLY = meanAbsPhi(30.0);
  const double PHI_LATE = meanAbsPhi(100.0);
  ASSERT_GT(PHI_EARLY, 1e-5) << "aileron pulse produced no bank response";

  // Near-neutral both ways: no wind-up (a truly unstable spiral would
  // grow visibly across 70 s) and no bleed-off (a decay here would
  // mean the fast roll subsidence swallowed the disturbance instead
  // of leaving it to the spiral). +/-50% over 70 s bounds |lambda|
  // below ~6e-3 1/s while the measured value sits near 3e-5.
  EXPECT_LT(PHI_LATE, 1.5 * PHI_EARLY) << "spiral wound up: " << PHI_EARLY << " -> " << PHI_LATE;
  EXPECT_GT(PHI_LATE, 0.5 * PHI_EARLY)
      << "bank bled off on the roll-subsidence timescale: " << PHI_EARLY << " -> " << PHI_LATE;
}
