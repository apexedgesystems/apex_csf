#ifndef APEX_MATH_FRAMES_CATALOG_HPP
#define APEX_MATH_FRAMES_CATALOG_HPP
/**
 * @file Catalog.hpp
 * @brief The standard celestial frame catalog: one epoch, two trees.
 *
 * Populates a FrameGraph with the well-known frames so users reach for a
 * standard one before defining their own:
 *
 *   ECI (root) --theta_earth(t)--> ECEF     Earth tree
 *   MCI (root) --theta_moon(t)-->  MCMF     Moon tree (tidally locked)
 *
 * Site ENU/NED frames attach to ECEF with the Geodetic.hpp edge builders;
 * vehicles attach below those (or below ECEF directly) with state-driven
 * edges; HCI has a reserved slot in CatalogIds and gains an edge with
 * future ephemeris work.
 *
 * EPOCH: one sim epoch for everything. The Epoch context fixes the Julian
 * date at sim t = 0; every time-driven edge derives its angle from
 * epoch + t, so replay is exact and no edge ever reads a wall clock. The
 * rung-1 inertial realization is a constant-rate rotation about +Z with
 * theta0 taken from the linear GMST relation at the epoch; precession,
 * nutation, and polar motion are deliberately not modeled at this rung
 * (upgrade path: a higher-fidelity time-driven edge, same graph shape).
 *
 * LIFETIME: the graph stores a POINTER to the Epoch (Delegate context) --
 * the Epoch must outlive the graph, exactly like every provider context.
 *
 * @note RT-SAFE: All operations noexcept, no allocation.
 */

#include "src/utilities/math/vecmat/inc/Angles.hpp"
#include "src/utilities/math/celestial/inc/EarthConstants.hpp"
#include "src/utilities/math/celestial/inc/MoonConstants.hpp"
#include "src/utilities/math/celestial/inc/TimeConstants.hpp"
#include "src/utilities/math/frames/inc/FrameGraph.hpp"
#include "src/utilities/math/frames/inc/FramesStatus.hpp"
#include "src/utilities/math/frames/inc/Transform.hpp"

#include <stdint.h>

namespace apex {
namespace math {
namespace frames {

/* --------------------------------- Epoch ---------------------------------- */

/**
 * @brief The single sim epoch: Julian date at sim t = 0, with the derived
 *        rotation anchors both time-driven catalog edges read.
 */
struct Epoch {
  double jd0 = celestial::JD_J2000; ///< Julian date at t = 0
  double thetaEarth0 = 0.0;         ///< Greenwich sidereal angle at t = 0 [rad]
  double thetaMoon0 = 0.0;          ///< lunar prime-meridian angle at t = 0 [rad]

  /** @brief Anchor the rotation angles from the Julian date (rung 1). */
  void init(double julianDateAtT0, double moonTheta0 = 0.0) noexcept {
    jd0 = julianDateAtT0;
    const double D = jd0 - celestial::JD_J2000;
    double theta =
        celestial::earth::GMST_AT_J2000_RAD + celestial::earth::GMST_RATE_RAD_PER_DAY * D;
    // Wrap to [0, 2*pi) without fmod (freestanding-friendly for large D).
    theta = theta -
            vecmat::TWO_PI * static_cast<double>(static_cast<long long>(theta / vecmat::TWO_PI));
    if (theta < 0.0) {
      theta += vecmat::TWO_PI;
    }
    thetaEarth0 = theta;
    thetaMoon0 = moonTheta0;
  }
};

/* ------------------------------ Catalog edges ------------------------------ */

/** @brief ECEF-child-of-ECI edge: rotation about +Z by theta0 + omega*t. */
template <typename T> uint8_t earthRotationEdge(void* ctx, T t, Transform<T>* out) noexcept {
  const auto* E = static_cast<const Epoch*>(ctx);
  *out = Transform<T>{};
  const T THETA = static_cast<T>(E->thetaEarth0) + static_cast<T>(celestial::earth::OMEGA) * t;
  (void)out->rotation().setFromAngleAxis(THETA, T(0), T(0), T(1));
  return 0;
}

/** @brief MCMF-child-of-MCI edge: tidally locked rotation about +Z. */
template <typename T> uint8_t moonRotationEdge(void* ctx, T t, Transform<T>* out) noexcept {
  const auto* E = static_cast<const Epoch*>(ctx);
  *out = Transform<T>{};
  const T THETA = static_cast<T>(E->thetaMoon0) + static_cast<T>(celestial::moon::OMEGA) * t;
  (void)out->rotation().setFromAngleAxis(THETA, T(0), T(0), T(1));
  return 0;
}

/* -------------------------------- Catalog --------------------------------- */

/** @brief Handles to the standard frames a built catalog provides. */
struct CatalogIds {
  FrameId eci = K_NO_FRAME;
  FrameId ecef = K_NO_FRAME;
  FrameId mci = K_NO_FRAME;
  FrameId mcmf = K_NO_FRAME;
  FrameId hci = K_NO_FRAME; ///< reserved: no edge until ephemeris work lands
};

/**
 * @brief Populate a graph with the standard catalog.
 *
 * @param g     The graph (needs 4 free slots).
 * @param epoch The sim epoch; MUST outlive the graph (Delegate context).
 * @param out   The standard frame handles.
 */
template <typename T, size_t CAPACITY>
inline uint8_t buildCatalog(FrameGraph<T, CAPACITY>& g, const Epoch& epoch,
                            CatalogIds& out) noexcept {
  using Provider = typename FrameGraph<T, CAPACITY>::EdgeProvider;
  // Delegate contexts are non-owning void*; the epoch outlives the graph by
  // contract (documented above), so the const_cast never enables a write.
  void* ctx = const_cast<Epoch*>(&epoch);

  uint8_t rc = g.addRoot("eci", out.eci);
  if (rc != 0) {
    return rc;
  }
  rc = g.addTimeDriven(out.eci, Provider{&earthRotationEdge<T>, ctx}, "ecef", out.ecef);
  if (rc != 0) {
    return rc;
  }
  rc = g.addRoot("mci", out.mci);
  if (rc != 0) {
    return rc;
  }
  return g.addTimeDriven(out.mci, Provider{&moonRotationEdge<T>, ctx}, "mcmf", out.mcmf);
}

} // namespace frames
} // namespace math
} // namespace apex

#endif // APEX_MATH_FRAMES_CATALOG_HPP
