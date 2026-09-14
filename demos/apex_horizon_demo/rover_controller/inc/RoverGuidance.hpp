#ifndef APEX_HORIZON_DEMO_ROVER_GUIDANCE_HPP
#define APEX_HORIZON_DEMO_ROVER_GUIDANCE_HPP
/**
 * @file RoverGuidance.hpp
 * @brief The rover's waypoint law as freestanding arithmetic.
 *
 * One function turns a pose, a speed and a leg into a steering angle
 * and a throttle. It has no component, no logging and no filesystem
 * behind it, only C's <math.h> (the firmware links libc and libm, no
 * C++ standard library), so the same header compiles into the host
 * controller and into the board's firmware: whatever the closed-loop
 * tests pin on the host is what the board runs.
 *
 * The law: pure pursuit along the leg line. A leg runs from where the
 * rover was when the target was set to the target. The aim point sits
 * lookahead_m ahead of the rover's projection onto that line and runs
 * past the target, so an off-axis start converges onto the leg and the
 * last metres are driven straight along it, arriving aligned; a rover
 * that is not lined up close to the end swings out through a keyhole
 * and rejoins the line short of the target. Beyond
 * 90 degrees of error the pursuit curvature falls toward zero (a
 * straight reversal would drive away), so the rover commits to full
 * lock until the aim point is ahead. Speed is trapezoidal: cruise,
 * then sqrt(2 a d) onto the target under the plant's braking limit,
 * slower through a corner. The heading rate is bounded by limiting the
 * lock against the faster of the current and commanded speed.
 */

#include <math.h>
#include <stdint.h>

namespace appsim {
namespace rover_controller {

/* ----------------------------- Constants ----------------------------- */

/// Freestanding min / max / clamp (the firmware has no <algorithm>).
inline double guidanceMin(double a, double b) noexcept { return (b < a) ? b : a; }
inline double guidanceMax(double a, double b) noexcept { return (a < b) ? b : a; }
inline double guidanceClamp(double v, double lo, double hi) noexcept {
  return (v < lo) ? lo : ((hi < v) ? hi : v);
}

inline constexpr double GUIDANCE_PI = 3.14159265358979323846;
inline constexpr double GUIDANCE_DEG_TO_RAD = GUIDANCE_PI / 180.0;
inline constexpr double GUIDANCE_RAD_TO_DEG = 180.0 / GUIDANCE_PI;

/* ----------------------------- GuidanceTunables ----------------------------- */

/// The numbers the law runs on; the host controller's tunables carry
/// the same fields in the same order, the firmware carries defaults.
struct GuidanceTunables {
  double wheelbase_m{1.5};
  double max_steer_deg{33.0};
  double lookahead_m{2.5};
  double cruise_throttle_frac{0.375}; ///< 3 m/s of 8.
  double brake_m_s2{2.0};
  double corner_speed_frac{0.2};
  double corner_deg{15.0};
  double arrival_tolerance_m{0.2};
  double passed_end_cross_m{0.5};
  double max_yaw_rate_deg_s{15.0};
};

/* ----------------------------- GuidanceLeg ----------------------------- */

/// Leg phases. ALONG: pursue the leg line. KEYHOLE: the rover is not
/// lined up near the end (or is past it): drive to the point a keyhole
/// short of the target. REJOIN: close to that point, turn onto the line;
/// back to ALONG once lined up. The widely separated entry and exit
/// angles (60 and 30 deg) keep the phase from flapping at a boundary.
inline constexpr uint8_t LEG_PHASE_ALONG = 0;
inline constexpr uint8_t LEG_PHASE_KEYHOLE = 1;
inline constexpr uint8_t LEG_PHASE_REJOIN = 2;

/// The current leg and its arrival latch.
struct GuidanceLeg {
  double target_north_m{0.0};
  double target_east_m{0.0};
  double start_north_m{0.0};
  double start_east_m{0.0};
  uint8_t valid{0};
  uint8_t arrived{0}; ///< Latched until the next target.
  uint8_t phase{0};   ///< LEG_PHASE_*: how the rover is approaching this leg.
};

/* ----------------------------- GuidanceInput ----------------------------- */

struct GuidanceInput {
  double north_m{0.0};
  double east_m{0.0};
  double heading_deg{0.0}; ///< Clockwise from north.
  double speed_m_s{0.0};
  double max_speed_m_s{8.0}; ///< The plant's full-throttle speed.
};

/* ----------------------------- GuidanceOutput ----------------------------- */

struct GuidanceOutput {
  double steer_deg{0.0};
  double throttle_frac{0.0};
  double distance_m{0.0};
  double bearing_deg{0.0};       ///< To the aim point, clockwise from north.
  double heading_error_deg{0.0}; ///< Wrapped bearing - heading [-180, 180).
  double cross_track_m{0.0};     ///< Signed distance from the leg line (+ right of it).
};

/* ----------------------------- Functions ----------------------------- */

/// Wrap an angle difference to [-180, 180) degrees.
inline double wrapDeg180(double deg) noexcept {
  double d = fmod(deg + 180.0, 360.0);
  if (d < 0.0) {
    d += 360.0;
  }
  return d - 180.0;
}

/// Start a leg from the rover's current position to (north, east).
inline void setLeg(GuidanceLeg& leg, double target_north_m, double target_east_m,
                   double here_north_m, double here_east_m) noexcept {
  leg.start_north_m = here_north_m;
  leg.start_east_m = here_east_m;
  leg.target_north_m = target_north_m;
  leg.target_east_m = target_east_m;
  leg.valid = 1u;
  leg.arrived = 0u;
  leg.phase = LEG_PHASE_ALONG;
}

/**
 * @brief One step of the waypoint law.
 *
 * Latches leg.arrived when the target is inside the tolerance ring or
 * the rover has passed the leg's end within passed_end_cross_m of the
 * line; while latched the output is zero steer and zero throttle so the
 * plant coasts to rest.
 */
inline GuidanceOutput waypointGuidance(const GuidanceTunables& p, GuidanceLeg& leg,
                                       const GuidanceInput& in) noexcept {
  GuidanceOutput out{};
  if (leg.valid == 0u) {
    return out;
  }
  const double DN = leg.target_north_m - in.north_m;
  const double DE = leg.target_east_m - in.east_m;
  const double DIST = sqrt(DN * DN + DE * DE);
  out.distance_m = DIST;

  // The leg as a line: unit direction from its start to the target,
  // and the rover's projection onto it (along, and signed cross-track,
  // + to the right of the direction of travel).
  const double LN = leg.target_north_m - leg.start_north_m;
  const double LE = leg.target_east_m - leg.start_east_m;
  const double LEG = sqrt(LN * LN + LE * LE);
  const bool HAS_LINE = LEG > 0.5;
  const double UN = HAS_LINE ? LN / LEG : 0.0;
  const double UE = HAS_LINE ? LE / LEG : 0.0;
  const double PN = in.north_m - leg.start_north_m;
  const double PE = in.east_m - leg.start_east_m;
  const double ALONG = HAS_LINE ? (PN * UN + PE * UE) : 0.0;
  const double CROSS = HAS_LINE ? (PE * UN - PN * UE) : 0.0;
  out.cross_track_m = CROSS;

  if (DIST < p.arrival_tolerance_m || (HAS_LINE && ALONG >= LEG && DIST < p.passed_end_cross_m)) {
    leg.arrived = 1u;
  }
  if (leg.arrived != 0u) {
    out.bearing_deg = fmod(atan2(DE, DN) * GUIDANCE_RAD_TO_DEG + 360.0, 360.0);
    return out;
  }

  // Aim point. ALONG: on the leg line, lookahead_m ahead of the rover's
  // projection and allowed to run past the target, so the approach is
  // driven straight along the leg. A rover more than 60 deg off the
  // line's direction with less than a keyhole (two turning radii plus
  // the lookahead) left before the end, or past the end, enters KEYHOLE
  // and drives to the line a keyhole short of the target; within a
  // turning radius of that point it REJOINs (pursues the line again)
  // and is ALONG once within 30 deg of it. The rover therefore meets the
  // end straight rather than circling a target inside its turning
  // circle. On a leg too short to define a line, aim at the target.
  double AN = DN;
  double AE = DE;
  double AIM_DIST = guidanceClamp(p.lookahead_m, 0.5, guidanceMax(DIST, 0.5));
  if (HAS_LINE) {
    const double R_MIN =
        p.wheelbase_m / tan(guidanceClamp(p.max_steer_deg, 1.0, 89.0) * GUIDANCE_DEG_TO_RAD);
    const double KEYHOLE = 2.0 * R_MIN + p.lookahead_m;
    const double LINE_ERR = fabs(wrapDeg180(atan2(UE, UN) * GUIDANCE_RAD_TO_DEG - in.heading_deg));
    const double KEY_N = leg.start_north_m + UN * (LEG - KEYHOLE);
    const double KEY_E = leg.start_east_m + UE * (LEG - KEYHOLE);
    if (leg.phase != LEG_PHASE_KEYHOLE &&
        (ALONG >= LEG ||
         (leg.phase == LEG_PHASE_ALONG && LINE_ERR > 60.0 && LEG - ALONG < KEYHOLE))) {
      leg.phase = LEG_PHASE_KEYHOLE;
    }
    if (leg.phase == LEG_PHASE_KEYHOLE) {
      const double KN = KEY_N - in.north_m;
      const double KE = KEY_E - in.east_m;
      if (sqrt(KN * KN + KE * KE) < R_MIN && ALONG < LEG) {
        leg.phase = LEG_PHASE_REJOIN;
      }
    }
    if (leg.phase == LEG_PHASE_REJOIN && LINE_ERR < 30.0) {
      leg.phase = LEG_PHASE_ALONG;
    }
    const double AIM_ALONG =
        (leg.phase == LEG_PHASE_KEYHOLE) ? LEG - KEYHOLE : ALONG + p.lookahead_m;
    AN = leg.start_north_m + UN * AIM_ALONG - in.north_m;
    AE = leg.start_east_m + UE * AIM_ALONG - in.east_m;
    AIM_DIST = guidanceMax(sqrt(AN * AN + AE * AE), 0.5);
  }
  const double BEARING = fmod(atan2(AE, AN) * GUIDANCE_RAD_TO_DEG + 360.0, 360.0);
  const double ERR = wrapDeg180(BEARING - in.heading_deg);
  out.bearing_deg = BEARING;
  out.heading_error_deg = ERR;

  // Pure pursuit onto the aim point (kappa = 2 sin(alpha) / d, delta =
  // atan(L kappa)); full lock while the aim point is behind.
  double steer = 0.0;
  if (fabs(ERR) > 90.0) {
    steer = copysign(p.max_steer_deg, ERR);
  } else {
    const double ALPHA = ERR * GUIDANCE_DEG_TO_RAD;
    const double KAPPA = 2.0 * sin(ALPHA) / AIM_DIST;
    steer = guidanceClamp(atan(p.wheelbase_m * KAPPA) * GUIDANCE_RAD_TO_DEG, -p.max_steer_deg,
                          p.max_steer_deg);
  }

  // Trapezoidal speed: cruise until the braking distance to the
  // tolerance ring, then sqrt(2 a d); slower through a corner.
  const double MAX_V = in.max_speed_m_s;
  const double CRUISE = p.cruise_throttle_frac * MAX_V;
  const double D_LEFT = guidanceMax(DIST - p.arrival_tolerance_m, 0.0);
  double v = guidanceMin(CRUISE, sqrt(2.0 * guidanceMax(p.brake_m_s2, 0.01) * D_LEFT));
  if (fabs(ERR) > p.corner_deg) {
    v = guidanceMin(v, CRUISE * p.corner_speed_frac);
  }
  out.throttle_frac = (MAX_V > 0.0) ? guidanceClamp(v / MAX_V, 0.0, 1.0) : 0.0;

  // Yaw-rate bound against the faster of the current and commanded
  // speed: tan(delta) <= rate L / v.
  const double V_BOUND = guidanceMax(in.speed_m_s, v);
  if (V_BOUND > 0.05 && p.max_yaw_rate_deg_s > 0.0) {
    const double LOCK = atan(p.max_yaw_rate_deg_s * GUIDANCE_DEG_TO_RAD * p.wheelbase_m / V_BOUND) *
                        GUIDANCE_RAD_TO_DEG;
    steer = guidanceClamp(steer, -LOCK, LOCK);
  }
  out.steer_deg = steer;
  return out;
}

} // namespace rover_controller
} // namespace appsim

#endif // APEX_HORIZON_DEMO_ROVER_GUIDANCE_HPP
