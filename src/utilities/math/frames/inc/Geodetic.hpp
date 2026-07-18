#ifndef APEX_MATH_FRAMES_GEODETIC_HPP
#define APEX_MATH_FRAMES_GEODETIC_HPP
/**
 * @file Geodetic.hpp
 * @brief Geodetic <-> ECEF conversions and local-tangent site edges.
 *
 * The canonical implementations for the tree (the gravity/terrain/GPS
 * copies migrate onto these): WGS-84 closed-form geodetic->ECEF, Bowring's
 * iterative ECEF->geodetic, and the ENU/NED site-frame constructors that
 * turn a geodetic site into a FrameGraph static edge (child = the local
 * tangent frame, parent = ECEF).
 *
 * Conventions:
 *  - Geodetic latitude/longitude in radians, height in meters above the
 *    WGS-84 ellipsoid.
 *  - ENU: x east, y north, z up. NED: x north, y east, z down (the
 *    aero/GNC first-class convention).
 *
 * Double-precision math throughout the site constructors is intentional:
 * ECEF magnitudes quantize at the meter in float32 (see the README float
 * posture); float graphs should receive already-localized frames.
 *
 * @note RT-SAFE: All functions noexcept, no allocation.
 */

#include "src/utilities/compatibility/inc/compat_math.hpp"
#include "src/utilities/math/celestial/inc/EarthConstants.hpp"
#include "src/utilities/math/frames/inc/Transform.hpp"
#include "src/utilities/math/vecmat/inc/Vec3Ops.hpp"

#include <stdint.h>

namespace apex {
namespace math {
namespace frames {

/**
 * @brief Geodetic (lat, lon, h) -> ECEF, WGS-84 closed form.
 */
template <typename T> inline void geodeticToEcefInto(T latRad, T lonRad, T hM, T* ecef) noexcept {
  const T SLAT = apex::compat::sin(latRad);
  const T CLAT = apex::compat::cos(latRad);
  const T SLON = apex::compat::sin(lonRad);
  const T CLON = apex::compat::cos(lonRad);
  const T A = T(celestial::earth::A);
  const T E2 = T(celestial::earth::E2);
  const T N = A / apex::compat::sqrt(T(1) - E2 * SLAT * SLAT); // prime vertical
  ecef[0] = (N + hM) * CLAT * CLON;
  ecef[1] = (N + hM) * CLAT * SLON;
  ecef[2] = (N * (T(1) - E2) + hM) * SLAT;
}

/**
 * @brief ECEF -> geodetic, Bowring's method (three iterations).
 */
template <typename T>
inline void ecefToGeodeticInto(const T* ecef, T& latRad, T& lonRad, T& hM) noexcept {
  const T A = T(celestial::earth::A);
  const T B = T(celestial::earth::B);
  const T E2 = T(celestial::earth::E2);
  const T EP2 = T(celestial::earth::EP2);

  const T X = ecef[0], Y = ecef[1], Z = ecef[2];
  const T P = apex::compat::sqrt(X * X + Y * Y);
  lonRad = apex::compat::atan2(Y, X);

  // Bowring: iterate the reduced latitude.
  T beta = apex::compat::atan2(A * Z, B * P);
  for (int i = 0; i < 3; ++i) {
    const T SB = apex::compat::sin(beta);
    const T CB = apex::compat::cos(beta);
    latRad = apex::compat::atan2(Z + EP2 * B * SB * SB * SB, P - E2 * A * CB * CB * CB);
    beta = apex::compat::atan2(B * apex::compat::sin(latRad), A * apex::compat::cos(latRad));
  }
  const T SLAT = apex::compat::sin(latRad);
  const T N = A / apex::compat::sqrt(T(1) - E2 * SLAT * SLAT);
  const T CLAT = apex::compat::cos(latRad);
  // Height from whichever axis is better conditioned at this latitude.
  hM = apex::compat::fabs(CLAT) > T(0.1) ? P / CLAT - N : Z / SLAT - N * (T(1) - E2);
}

/**
 * @brief ECEF-child edge for an ENU site frame at (lat, lon, h).
 *
 * The Transform maps ENU coordinates into ECEF: rotation columns are the
 * east/north/up unit vectors; translation is the site's ECEF position.
 */
template <typename T>
inline void enuSiteEdgeInto(T latRad, T lonRad, T hM, Transform<T>& out) noexcept {
  const T SLAT = apex::compat::sin(latRad);
  const T CLAT = apex::compat::cos(latRad);
  const T SLON = apex::compat::sin(lonRad);
  const T CLON = apex::compat::cos(lonRad);
  // Row-major R(ecef <- enu): columns = e_east, e_north, e_up in ECEF.
  const T M[9] = {-SLON,       -SLAT * CLON, CLAT * CLON, CLON, -SLAT * SLON,
                  CLAT * SLON, T(0),         CLAT,        SLAT};
  (void)out.rotation().setFromRotationMatrix(M);
  geodeticToEcefInto(latRad, lonRad, hM, out.t);
}

/**
 * @brief ECEF-child edge for an NED site frame at (lat, lon, h).
 *
 * NED is the aero/GNC first-class local frame: x north, y east, z down.
 */
template <typename T>
inline void nedSiteEdgeInto(T latRad, T lonRad, T hM, Transform<T>& out) noexcept {
  const T SLAT = apex::compat::sin(latRad);
  const T CLAT = apex::compat::cos(latRad);
  const T SLON = apex::compat::sin(lonRad);
  const T CLON = apex::compat::cos(lonRad);
  // Row-major R(ecef <- ned): columns = e_north, e_east, e_down in ECEF.
  const T M[9] = {-SLAT * CLON, -SLON, -CLAT * CLON, -SLAT * SLON, CLON,
                  -CLAT * SLON, CLAT,  T(0),         -SLAT};
  (void)out.rotation().setFromRotationMatrix(M);
  geodeticToEcefInto(latRad, lonRad, hM, out.t);
}

} // namespace frames
} // namespace math
} // namespace apex

#endif // APEX_MATH_FRAMES_GEODETIC_HPP
