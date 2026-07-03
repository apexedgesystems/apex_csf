#ifndef APEX_HORIZON_DEMO_LIDAR_BOX_TYPES_HPP
#define APEX_HORIZON_DEMO_LIDAR_BOX_TYPES_HPP
/**
 * @file LidarBoxTypes.hpp
 * @brief Wire contract + component data structs for the lidar_box demo.
 *
 * The wire contract (LBOX/v1) is shared with the out-of-process visualizer:
 * the consumer owns the canonical contract header and this file matches it
 * byte-for-byte (the static_asserts below pin the layout). Bump kAppVersion on
 * any LidarBoxFrame layout change -- the consumer validates it on attach.
 *
 * Frame + constants use float (the wire type); the producer's internal
 * kinematics run in double and narrow only when filling the frame.
 */

#include <cstddef>
#include <cstdint>

namespace appsim {
namespace lidar_box {

/* ----------------------------- Channel identity (LBOX/v1) ----------------------------- */

inline constexpr std::uint32_t kAppMagic = 0x4C424F58u; // "LBOX"
inline constexpr std::uint16_t kAppVersion = 1;

/* ----------------------------- Box constants (shared, compile-time) -----------------------------
 */

/// Asymmetric box, origin at center, meters. Shared so the producer's clearance
/// math and the consumer's rendered box cannot drift.
inline constexpr float kBoxHalfX = 4.0f;
inline constexpr float kBoxHalfY = 3.0f;
inline constexpr float kBoxHalfZ = 2.5f;
/// Body clearance inset: the body center stays within +/-(half - radius), so
/// every wall clearance is at least kBodyRadius.
inline constexpr float kBodyRadius = 0.5f;

/* ----------------------------- LidarBoxFrame (Ring A egress slot) ----------------------------- */

/**
 * @struct LidarBoxFrame
 * @brief producer -> consumer egress frame, ~50 Hz. 48 bytes, no padding.
 *
 * Box-local right-handed frame: +X/+Y horizontal, +Z up, origin at box center,
 * meters. Clearances are measured from the body center (the sensor mount) to
 * each wall along the axes; always positive while the body respects the inset.
 */
struct LidarBoxFrame {
  float pos_x = 0.0f; ///< body center, box-local [m]
  float pos_y = 0.0f;
  float pos_z = 0.0f;
  float yaw_rad = 0.0f; ///< spin about box +Z [rad]

  float clr_pos_x = 0.0f; ///< sensor-mount -> +X wall clearance [m]
  float clr_neg_x = 0.0f; ///< -> -X wall
  float clr_pos_y = 0.0f;
  float clr_neg_y = 0.0f;
  float clr_pos_z = 0.0f;
  float clr_neg_z = 0.0f;

  std::uint64_t timestamp_ns = 0; ///< monotonic clock [ns]
};

static_assert(sizeof(LidarBoxFrame) == 48, "LBOX/v1 fixes the frame at 48 bytes");
static_assert(alignof(LidarBoxFrame) == 8, "LBOX/v1 fixes the frame alignment at 8");
static_assert(offsetof(LidarBoxFrame, yaw_rad) == 12, "LBOX/v1 layout drift");
static_assert(offsetof(LidarBoxFrame, clr_pos_x) == 16, "LBOX/v1 layout drift");
static_assert(offsetof(LidarBoxFrame, timestamp_ns) == 40, "LBOX/v1 layout drift");

/* ----------------------------- LidarBoxTunables (TPRM) ----------------------------- */

/**
 * @struct LidarBoxTunables
 * @brief TPRM-loadable configuration for the producer. 88 bytes, flat POD.
 *
 * The drift is a 3D Lissajous figure: pos_axis = amp_axis * sin(omega_axis * t
 * + phase_axis), with amp_axis = amp_frac_axis * (half_axis - kBodyRadius).
 * Incommensurate omegas keep the path from closing, so all six clearances
 * animate. Yaw advances at a constant rate and wraps.
 */
struct LidarBoxTunables {
  double dt_s{0.02}; ///< kinematic step; matches the 50 Hz schedule

  double amp_frac_x{0.85}; ///< amplitude as a fraction of (half - radius), <= 1
  double amp_frac_y{0.90};
  double amp_frac_z{0.80};

  double omega_x_rad_s{0.11}; ///< incommensurate drift frequencies
  double omega_y_rad_s{0.17};
  double omega_z_rad_s{0.23};

  double phase_y_rad{1.0472}; ///< ~60 deg, de-phases the axes
  double phase_z_rad{2.0944}; ///< ~120 deg

  double yaw_rate_rad_s{0.35}; ///< steady spin about +Z

  double sigma_m{0.0}; ///< per-beam lidar range noise (1-sigma, m); 0 = ideal
};

static_assert(sizeof(LidarBoxTunables) == 88, "LidarBoxTunables layout drift");

/* ----------------------------- LidarBoxState (STATE) ----------------------------- */

/** @struct LidarBoxState @brief Internal kinematic state. 40 bytes. */
struct LidarBoxState {
  double t_s{0.0}; ///< sim time since init [s]
  double pos_x_m{0.0};
  double pos_y_m{0.0};
  double pos_z_m{0.0};
  double yaw_rad{0.0};
};

static_assert(sizeof(LidarBoxState) == 40, "LidarBoxState layout drift");

} // namespace lidar_box
} // namespace appsim

#endif // APEX_HORIZON_DEMO_LIDAR_BOX_TYPES_HPP
