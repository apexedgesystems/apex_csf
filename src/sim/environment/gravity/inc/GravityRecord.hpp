#ifndef APEX_SIM_ENVIRONMENT_GRAVITY_RECORD_HPP
#define APEX_SIM_ENVIRONMENT_GRAVITY_RECORD_HPP
/**
 * @file GravityRecord.hpp
 * @brief Binary record format definitions for spherical harmonic coefficients.
 *
 * Defines the packed binary formats for gravity coefficient storage.
 * These formats are the production artifact layouts consumed by the
 * gravity library at runtime. Compatible with EGM2008 (Earth) and
 * GRGM1200A (Moon) coefficient files.
 *
 * Record layout (36 bytes, no file header, sequential records):
 *   int16  n        Degree (0-2190 for Earth, 0-1200 for Moon)
 *   int16  m        Order (0-n)
 *   double Cbar     Fully-normalized cosine coefficient
 *   double Sbar     Fully-normalized sine coefficient
 *   double sigmaC   1-sigma uncertainty for Cbar
 *   double sigmaS   1-sigma uncertainty for Sbar
 *
 * Binary compatibility:
 *   - Packed to exactly 36 contiguous bytes (no padding)
 *   - Host-endian (little-endian on x86/ARM)
 *   - Designed for fast sequential ingestion
 */

#include <cstddef>
#include <cstdint>

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- Constants ----------------------------- */

/// Record size in bytes (2+2+8+8+8+8 = 36).
inline constexpr std::size_t K_GRAVITY_RECORD_SIZE = 36;

/* ----------------------------- GravityRecord ----------------------------- */

/**
 * @brief 36-byte packed record (n, m, Cbar, Sbar, sigmaC, sigmaS).
 *
 * The packing pragma enforces the on-disk size (36 bytes). Reads and writes
 * occur as single contiguous blocks to preserve layout and performance.
 */
#pragma pack(push, 1)
struct GravityRecord {
  std::int16_t n; ///< Degree.
  std::int16_t m; ///< Order (0 to n).
  double Cbar;    ///< Fully-normalized cosine coefficient.
  double Sbar;    ///< Fully-normalized sine coefficient.
  double sigmaC;  ///< 1-sigma uncertainty for Cbar.
  double sigmaS;  ///< 1-sigma uncertainty for Sbar.
};
#pragma pack(pop)

static_assert(sizeof(GravityRecord) == K_GRAVITY_RECORD_SIZE, "GravityRecord must be 36 bytes");

/* ----------------------------- Slim Record Types ----------------------------- */

#pragma pack(push, 1)

/// No sigmas, double precision (20 bytes: n,m,C,S).
struct GravityNSRecordD {
  std::int16_t n; ///< Degree.
  std::int16_t m; ///< Order.
  double Cbar;    ///< Cosine coefficient.
  double Sbar;    ///< Sine coefficient.
};
static_assert(sizeof(GravityNSRecordD) == 20, "GravityNSRecordD must be 20 bytes");

/// No sigmas, single precision (12 bytes: n,m,c,s).
struct GravityNSRecordF {
  std::int16_t n; ///< Degree.
  std::int16_t m; ///< Order.
  float cbar;     ///< Cosine coefficient.
  float sbar;     ///< Sine coefficient.
};
static_assert(sizeof(GravityNSRecordF) == 12, "GravityNSRecordF must be 12 bytes");

#pragma pack(pop)

} // namespace gravity
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_GRAVITY_RECORD_HPP
