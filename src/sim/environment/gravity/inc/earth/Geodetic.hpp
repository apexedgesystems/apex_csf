#ifndef APEX_SIM_ENVIRONMENT_GRAVITY_EARTH_GEODETIC_HPP
#define APEX_SIM_ENVIRONMENT_GRAVITY_EARTH_GEODETIC_HPP
/**
 * @file Geodetic.hpp
 * @brief Geodetic coordinate conversions and normal gravity.
 *
 * Provides conversions between ECEF (Earth-Centered Earth-Fixed) and
 * geodetic (latitude, longitude, altitude) coordinates, plus WGS84
 * normal gravity computation.
 */

#include "src/sim/environment/gravity/inc/earth/Wgs84Constants.hpp"

#include <cmath>

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- Geodetic Coordinates ----------------------------- */

/**
 * @brief Geodetic coordinates (WGS84).
 */
struct GeodeticCoord {
  double lat; ///< Geodetic latitude [rad], positive north.
  double lon; ///< Geodetic longitude [rad], positive east.
  double alt; ///< Altitude above ellipsoid [m].
};

/* ----------------------------- ECEF to Geodetic ----------------------------- */

/**
 * @brief Convert ECEF to geodetic coordinates (Bowring's iterative method).
 *
 * Uses 2-3 iterations of Bowring's method for sub-millimeter accuracy.
 *
 * @param ecef ECEF position [x, y, z] in meters.
 * @param geo Output geodetic coordinates.
 * @note RT-safe: No allocation, O(1).
 */
inline void ecefToGeodetic(const double ecef[3], GeodeticCoord& geo) noexcept {
  const double X = ecef[0];
  const double Y = ecef[1];
  const double Z = ecef[2];

  // Longitude is straightforward
  geo.lon = std::atan2(Y, X);

  // Distance from Z-axis
  const double P = std::sqrt(X * X + Y * Y);

  // Handle polar singularity
  if (P < 1e-10) {
    geo.lat = (Z >= 0.0) ? M_PI / 2.0 : -M_PI / 2.0;
    geo.alt = std::fabs(Z) - wgs84::B;
    return;
  }

  // Bowring's iterative method
  // Initial estimate using spherical approximation
  double lat = std::atan2(Z, P * (1.0 - wgs84::E2));

  // 2-3 iterations for sub-mm accuracy
  for (int i = 0; i < 3; ++i) {
    const double SIN_LAT = std::sin(lat);
    const double N = wgs84::A / std::sqrt(1.0 - wgs84::E2 * SIN_LAT * SIN_LAT);
    lat = std::atan2(Z + wgs84::E2 * N * SIN_LAT, P);
  }

  geo.lat = lat;

  // Compute altitude
  const double SIN_LAT = std::sin(lat);
  const double COS_LAT = std::cos(lat);
  const double N = wgs84::A / std::sqrt(1.0 - wgs84::E2 * SIN_LAT * SIN_LAT);

  if (std::fabs(COS_LAT) > 1e-10) {
    geo.alt = P / COS_LAT - N;
  } else {
    geo.alt = std::fabs(Z) / std::fabs(SIN_LAT) - N * (1.0 - wgs84::E2);
  }
}

/* ----------------------------- Geodetic to ECEF ----------------------------- */

/**
 * @brief Convert geodetic coordinates to ECEF.
 *
 * @param geo Geodetic coordinates.
 * @param ecef Output ECEF position [x, y, z] in meters.
 * @note RT-safe: No allocation, O(1).
 */
inline void geodeticToEcef(const GeodeticCoord& geo, double ecef[3]) noexcept {
  const double SIN_LAT = std::sin(geo.lat);
  const double COS_LAT = std::cos(geo.lat);
  const double SIN_LON = std::sin(geo.lon);
  const double COS_LON = std::cos(geo.lon);

  // Prime vertical radius of curvature
  const double N = wgs84::A / std::sqrt(1.0 - wgs84::E2 * SIN_LAT * SIN_LAT);

  ecef[0] = (N + geo.alt) * COS_LAT * COS_LON;
  ecef[1] = (N + geo.alt) * COS_LAT * SIN_LON;
  ecef[2] = (N * (1.0 - wgs84::E2) + geo.alt) * SIN_LAT;
}

/**
 * @brief Convert geodetic coordinates to ECEF (array version).
 *
 * @param lat Geodetic latitude [rad].
 * @param lon Geodetic longitude [rad].
 * @param alt Altitude above ellipsoid [m].
 * @param ecef Output ECEF position [x, y, z] in meters.
 * @note RT-safe: No allocation, O(1).
 */
inline void geodeticToEcef(double lat, double lon, double alt, double ecef[3]) noexcept {
  GeodeticCoord geo{lat, lon, alt};
  geodeticToEcef(geo, ecef);
}

/* ----------------------------- Degrees/Radians ----------------------------- */

/// Convert degrees to radians.
constexpr double degToRad(double deg) noexcept { return deg * M_PI / 180.0; }

/// Convert radians to degrees.
constexpr double radToDeg(double rad) noexcept { return rad * 180.0 / M_PI; }

/* ----------------------------- Normal Gravity ----------------------------- */

/**
 * @brief Compute WGS84 normal gravity at geodetic position.
 *
 * Uses the Somigliana closed-form formula for gravity on the ellipsoid,
 * then applies the free-air and second-order altitude corrections.
 *
 * @param lat Geodetic latitude [rad].
 * @param alt Altitude above ellipsoid [m].
 * @return Normal gravity magnitude [m/s^2].
 * @note RT-safe: No allocation, O(1).
 */
inline double normalGravity(double lat, double alt) noexcept {
  const double SIN_LAT = std::sin(lat);
  const double SIN_LAT2 = SIN_LAT * SIN_LAT;

  // Somigliana formula for gravity on the ellipsoid
  // gamma_0 = gamma_e * (1 + k*sin^2(lat)) / sqrt(1 - e^2*sin^2(lat))
  const double DENOM = std::sqrt(1.0 - wgs84::E2 * SIN_LAT2);
  const double GAMMA_0 = wgs84::GAMMA_E * (1.0 + wgs84::SOMIGLIANA_K * SIN_LAT2) / DENOM;

  if (std::fabs(alt) < 1e-6) {
    return GAMMA_0;
  }

  // Use inverse-square law for altitude correction
  // gamma = gamma_0 * (R_eff / (R_eff + h))^2
  // where R_eff is the effective radius at this latitude
  // For simplicity, use mean radius adjusted by latitude
  const double N = wgs84::A / std::sqrt(1.0 - wgs84::E2 * SIN_LAT2);
  const double R_EFF = N * std::sqrt(1.0 - wgs84::E2 * (2.0 - wgs84::E2) * SIN_LAT2);
  const double RATIO = R_EFF / (R_EFF + alt);

  return GAMMA_0 * RATIO * RATIO;
}

/**
 * @brief Compute WGS84 normal gravity at geodetic position.
 *
 * @param geo Geodetic coordinates.
 * @return Normal gravity magnitude [m/s^2].
 * @note RT-safe: No allocation, O(1).
 */
inline double normalGravity(const GeodeticCoord& geo) noexcept {
  return normalGravity(geo.lat, geo.alt);
}

/**
 * @brief Compute normal gravity vector in local NED frame.
 *
 * Normal gravity points along the local vertical (down direction in NED),
 * so the vector is [0, 0, gamma].
 *
 * @param lat Geodetic latitude [rad].
 * @param alt Altitude above ellipsoid [m].
 * @param g_ned Output gravity vector in NED [m/s^2].
 * @note RT-safe: No allocation, O(1).
 */
inline void normalGravityNed(double lat, double alt, double g_ned[3]) noexcept {
  g_ned[0] = 0.0;
  g_ned[1] = 0.0;
  g_ned[2] = normalGravity(lat, alt);
}

/* ----------------------------- Local Frame Rotations ----------------------------- */

/**
 * @brief Compute rotation matrix from ECEF to local NED frame.
 *
 * @param lat Geodetic latitude [rad].
 * @param lon Geodetic longitude [rad].
 * @param R_ned_ecef Output 3x3 rotation matrix (row-major, R[row*3+col]).
 * @note RT-safe: No allocation, O(1).
 */
inline void ecefToNedRotation(double lat, double lon, double R_ned_ecef[9]) noexcept {
  const double SIN_LAT = std::sin(lat);
  const double COS_LAT = std::cos(lat);
  const double SIN_LON = std::sin(lon);
  const double COS_LON = std::cos(lon);

  // Row 0: North
  R_ned_ecef[0] = -SIN_LAT * COS_LON;
  R_ned_ecef[1] = -SIN_LAT * SIN_LON;
  R_ned_ecef[2] = COS_LAT;

  // Row 1: East
  R_ned_ecef[3] = -SIN_LON;
  R_ned_ecef[4] = COS_LON;
  R_ned_ecef[5] = 0.0;

  // Row 2: Down
  R_ned_ecef[6] = -COS_LAT * COS_LON;
  R_ned_ecef[7] = -COS_LAT * SIN_LON;
  R_ned_ecef[8] = -SIN_LAT;
}

/**
 * @brief Transform vector from ECEF to local NED frame.
 *
 * @param lat Geodetic latitude [rad].
 * @param lon Geodetic longitude [rad].
 * @param v_ecef Input vector in ECEF.
 * @param v_ned Output vector in NED.
 * @note RT-safe: No allocation, O(1).
 */
inline void ecefToNed(double lat, double lon, const double v_ecef[3], double v_ned[3]) noexcept {
  double R[9];
  ecefToNedRotation(lat, lon, R);

  v_ned[0] = R[0] * v_ecef[0] + R[1] * v_ecef[1] + R[2] * v_ecef[2];
  v_ned[1] = R[3] * v_ecef[0] + R[4] * v_ecef[1] + R[5] * v_ecef[2];
  v_ned[2] = R[6] * v_ecef[0] + R[7] * v_ecef[1] + R[8] * v_ecef[2];
}

/**
 * @brief Transform vector from local NED to ECEF frame.
 *
 * @param lat Geodetic latitude [rad].
 * @param lon Geodetic longitude [rad].
 * @param v_ned Input vector in NED.
 * @param v_ecef Output vector in ECEF.
 * @note RT-safe: No allocation, O(1).
 */
inline void nedToEcef(double lat, double lon, const double v_ned[3], double v_ecef[3]) noexcept {
  double R[9];
  ecefToNedRotation(lat, lon, R);

  // Transpose multiplication (R^T * v_ned)
  v_ecef[0] = R[0] * v_ned[0] + R[3] * v_ned[1] + R[6] * v_ned[2];
  v_ecef[1] = R[1] * v_ned[0] + R[4] * v_ned[1] + R[7] * v_ned[2];
  v_ecef[2] = R[2] * v_ned[0] + R[5] * v_ned[1] + R[8] * v_ned[2];
}

} // namespace gravity
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_GRAVITY_EARTH_GEODETIC_HPP
