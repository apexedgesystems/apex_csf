#ifndef APEX_HORIZON_DEMO_LIDAR_BOX_TYPES_HPP
#define APEX_HORIZON_DEMO_LIDAR_BOX_TYPES_HPP
/**
 * @file LidarBoxTypes.hpp
 * @brief Wire contract + component data structs for the lidar_box demo.
 *
 * The wire contract (LBOX/v2) is shared with the out-of-process visualizer:
 * the consumer owns the canonical contract header and this file matches it
 * byte-for-byte (the static_asserts below pin the layout). Bump kAppVersion on
 * any LidarBoxFrame layout change -- the consumer validates it on attach.
 *
 * v2 semantics: a MOUNTED, body-fixed lidar. Six sensor pods sit at
 * `mount_radius` from the body center along the body axes; the X/Y pairs yaw
 * with the body, Z stays vertical. Each streamed distance is "pod tip to
 * wall", reaching 0.0 at contact. The frame also carries the scene block (box
 * half-extents + mount radius) so the consumer builds the room and standoffs
 * from streamed values -- apex tunables are the single configuration owner;
 * the compile-time constants below are seed defaults only.
 *
 * Frame + constants use float (the wire type); the producer's internal
 * kinematics run in double and narrow only when filling the frame.
 */

#include <cstddef>
#include <cstdint>

namespace appsim {
namespace lidar_box {

/* ----------------------------- Channel identity (LBOX/v2) ----------------------------- */

inline constexpr std::uint32_t kAppMagic = 0x4C424F58u; // "LBOX"
inline constexpr std::uint16_t kAppVersion = 2;

/* ----------------------------- Scene defaults (seed values) ----------------------------- */

/// Default asymmetric box, origin at center, meters. These seed the producer's
/// tunables (the runtime owner of the scene) and the consumer's loopback/test
/// defaults; the live values stream in every frame's scene block.
inline constexpr float kBoxHalfX = 4.0f;
inline constexpr float kBoxHalfY = 3.0f;
inline constexpr float kBoxHalfZ = 2.5f;
/// Default sensor mount offset from the body center. Doubles as the body
/// clearance inset: the center stays within +/-(half - mount_radius), so every
/// pod-tip distance is >= 0, reaching 0 exactly at wall contact.
inline constexpr float kMountRadius = 0.5f;

/* ----------------------------- LidarBoxFrame (Ring A egress slot) ----------------------------- */

/**
 * @struct LidarBoxFrame
 * @brief producer -> consumer egress frame, ~50 Hz. 64 bytes, no padding.
 *
 * Box-local right-handed frame: +X/+Y horizontal, +Z up, origin at box center,
 * meters. Distances are mounted-sensor ray ranges along the BODY axes from the
 * pod tips (bx = (cos yaw, sin yaw, 0), by = (-sin yaw, cos yaw, 0),
 * bz = (0, 0, 1)); >= 0 while the body respects the inset.
 */
struct LidarBoxFrame {
  float pos_x = 0.0f; ///< body center, box-local [m]
  float pos_y = 0.0f;
  float pos_z = 0.0f;
  float yaw_rad = 0.0f; ///< spin about box +Z [rad]

  float dist_bx_pos = 0.0f; ///< pod tip -> wall along body +X [m]
  float dist_bx_neg = 0.0f; ///< along body -X
  float dist_by_pos = 0.0f; ///< along body +Y
  float dist_by_neg = 0.0f; ///< along body -Y
  float dist_bz_pos = 0.0f; ///< along +Z (body Z stays vertical)
  float dist_bz_neg = 0.0f; ///< along -Z

  std::uint64_t timestamp_ns = 0; ///< monotonic clock [ns]

  /* -- scene block: the streamed configuration (apex tunables are the owner) -- */
  float box_half_x = 0.0f; ///< box half-extents [m]
  float box_half_y = 0.0f;
  float box_half_z = 0.0f;
  float mount_radius = 0.0f; ///< sensor mount offset from the body center [m]
};

static_assert(sizeof(LidarBoxFrame) == 64, "LBOX/v2 fixes the frame at 64 bytes");
static_assert(alignof(LidarBoxFrame) == 8, "LBOX/v2 fixes the frame alignment at 8");
static_assert(offsetof(LidarBoxFrame, yaw_rad) == 12, "LBOX/v2 layout drift");
static_assert(offsetof(LidarBoxFrame, dist_bx_pos) == 16, "LBOX/v2 layout drift");
static_assert(offsetof(LidarBoxFrame, dist_bz_neg) == 36, "LBOX/v2 layout drift");
static_assert(offsetof(LidarBoxFrame, timestamp_ns) == 40, "LBOX/v2 layout drift");
static_assert(offsetof(LidarBoxFrame, box_half_x) == 48, "LBOX/v2 layout drift");
static_assert(offsetof(LidarBoxFrame, mount_radius) == 60, "LBOX/v2 layout drift");

/* ----------------------------- LidarBoxTunables (TPRM) ----------------------------- */

/**
 * @struct LidarBoxTunables
 * @brief TPRM-loadable configuration for the producer. 120 bytes, flat POD.
 *
 * The drift is a 3D Lissajous figure: pos_axis = amp_axis * sin(omega_axis * t
 * + phase_axis), with amp_axis = amp_frac_axis * (half_axis - mount_radius).
 * Incommensurate omegas keep the path from closing, so all six distances
 * animate. Yaw advances at a constant rate and wraps.
 *
 * The scene lives here (the single owner) and streams in every frame's scene
 * block; the consumer renders whatever arrives.
 */
struct LidarBoxTunables {
  double dt_s{0.02}; ///< kinematic step; matches the 50 Hz schedule

  double amp_frac_x{0.85}; ///< amplitude as a fraction of (half - mount_radius), <= 1
  double amp_frac_y{0.90};
  double amp_frac_z{0.80};

  double omega_x_rad_s{0.11}; ///< incommensurate drift frequencies
  double omega_y_rad_s{0.17};
  double omega_z_rad_s{0.23};

  double phase_y_rad{1.0472}; ///< ~60 deg, de-phases the axes
  double phase_z_rad{2.0944}; ///< ~120 deg

  double yaw_rate_rad_s{0.35}; ///< steady spin about +Z

  double sigma_m{0.0}; ///< per-beam lidar range noise (1-sigma, m); 0 = ideal

  /* -- the scene (streamed in the frame's scene block) -- */
  double box_half_x_m{static_cast<double>(kBoxHalfX)};
  double box_half_y_m{static_cast<double>(kBoxHalfY)};
  double box_half_z_m{static_cast<double>(kBoxHalfZ)};
  double mount_radius_m{static_cast<double>(kMountRadius)};
};

static_assert(sizeof(LidarBoxTunables) == 120, "LidarBoxTunables layout drift");

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
