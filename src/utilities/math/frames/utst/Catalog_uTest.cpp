/**
 * @file Catalog_uTest.cpp
 * @brief Tests for the standard catalog: the epoch anchors, geodetic
 *        conversions (incl. equivalence with the gravity-library
 *        implementations they become canon for), site edges, and the
 *        two-tree catalog build.
 */

#include "src/utilities/math/vecmat/inc/Angles.hpp"
#include "src/utilities/math/celestial/inc/EarthConstants.hpp"
#include "src/utilities/math/frames/inc/Catalog.hpp"
#include "src/utilities/math/frames/inc/FrameGraph.hpp"
#include "src/utilities/math/frames/inc/Geodetic.hpp"

// Sim-side implementation this file's conversions are canon for; the
// equivalence lock guards the adoption-branch migration.
#include "src/sim/environment/gravity/inc/earth/Geodetic.hpp"

#include <cmath>
#include <gtest/gtest.h>

namespace fr = apex::math::frames;
namespace cel = apex::math::celestial;

namespace {
// The rung-1 model's own rotation period: 2*pi / OMEGA. (The UT1 sidereal
// day differs in the 4th decimal -- that distinction is rung-2 fidelity.)
constexpr double K_ROTATION_PERIOD_S =
    apex::math::vecmat::TWO_PI / apex::math::celestial::earth::OMEGA;
} // namespace

/* --------------------------------- Epoch ---------------------------------- */

/** @test The epoch anchors GMST: exact at J2000, rate-advanced after a day. */
TEST(CatalogEpochTest, GmstAnchors) {
  fr::Epoch e;
  e.init(cel::JD_J2000);
  EXPECT_NEAR(e.thetaEarth0, cel::earth::GMST_AT_J2000_RAD, 1e-12);

  fr::Epoch e1;
  e1.init(cel::JD_J2000 + 1.0);
  double want = cel::earth::GMST_AT_J2000_RAD + cel::earth::GMST_RATE_RAD_PER_DAY -
                apex::math::vecmat::TWO_PI;
  EXPECT_NEAR(e1.thetaEarth0, want, 1e-9);

  // Far from the epoch the wrap stays in [0, 2*pi).
  fr::Epoch e2;
  e2.init(cel::JD_J2000 + 9871.25);
  EXPECT_GE(e2.thetaEarth0, 0.0);
  EXPECT_LT(e2.thetaEarth0, apex::math::vecmat::TWO_PI);
}

/* ------------------------------- Geodetic --------------------------------- */

/** @test Anchor points: equator/prime meridian -> (A,0,0); pole -> (0,0,B). */
TEST(CatalogGeodeticTest, EcefAnchors) {
  double ecef[3];
  fr::geodeticToEcefInto(0.0, 0.0, 0.0, ecef);
  EXPECT_NEAR(ecef[0], cel::earth::A, 1e-6);
  EXPECT_NEAR(ecef[1], 0.0, 1e-6);
  EXPECT_NEAR(ecef[2], 0.0, 1e-6);

  fr::geodeticToEcefInto(apex::math::vecmat::HALF_PI, 0.0, 0.0, ecef);
  EXPECT_NEAR(ecef[0], 0.0, 1e-3);
  EXPECT_NEAR(ecef[2], cel::earth::B, 5e-5);
}

/** @test Round-trip geodetic -> ECEF -> geodetic across sites and heights. */
TEST(CatalogGeodeticTest, RoundTrip) {
  const double CASES[][3] = {
      {0.6, -2.1, 120.0},   // mid-latitude
      {-0.9, 0.4, 8848.0},  // southern, high terrain
      {1.4, 3.0, 35786e3},  // near-polar, GEO altitude
      {-1.5, -0.1, -100.0}, // near south pole, below ellipsoid
  };
  for (const auto& C : CASES) {
    double ecef[3], lat = 0, lon = 0, h = 0;
    fr::geodeticToEcefInto(C[0], C[1], C[2], ecef);
    fr::ecefToGeodeticInto(ecef, lat, lon, h);
    EXPECT_NEAR(lat, C[0], 1e-11);
    EXPECT_NEAR(lon, C[1], 1e-12);
    EXPECT_NEAR(h, C[2], 1e-6);
  }
}

/** @test Equivalence with the gravity-library implementations (the canon
 *        lock for the adoption-branch migration). */
TEST(CatalogGeodeticTest, MatchesGravityImplementation) {
  namespace gg = sim::environment::gravity;
  const double CASES[][3] = {
      {0.6, -2.1, 120.0}, {-0.9, 0.4, 8848.0}, {0.0, 0.0, 0.0}, {1.2, 2.9, 500e3}};
  for (const auto& C : CASES) {
    double ours[3], theirs[3];
    fr::geodeticToEcefInto(C[0], C[1], C[2], ours);
    gg::geodeticToEcef(C[0], C[1], C[2], theirs);
    for (int i = 0; i < 3; ++i) {
      EXPECT_NEAR(ours[i], theirs[i], 1e-6) << "fwd axis " << i;
    }

    double lat = 0, lon = 0, h = 0;
    fr::ecefToGeodeticInto(ours, lat, lon, h);
    gg::GeodeticCoord geo{};
    gg::ecefToGeodetic(theirs, geo);
    EXPECT_NEAR(lat, geo.lat, 1e-10);
    EXPECT_NEAR(lon, geo.lon, 1e-12);
    EXPECT_NEAR(h, geo.alt, 1e-5);
  }
}

/* ------------------------------- Site edges -------------------------------- */

/** @test ENU at the equator/prime meridian: E=+Y, N=+Z, U=+X (hand-derived). */
TEST(CatalogSiteTest, EnuAtEquatorPrimeMeridian) {
  fr::Transform<double> x;
  fr::enuSiteEdgeInto(0.0, 0.0, 0.0, x);

  const double UP[3] = {0, 0, 100};
  const double EAST[3] = {100, 0, 0};
  const double NORTH[3] = {0, 100, 0};
  double out[3];

  fr::transformPointInto(x, UP, out); // up -> +X, from the surface point
  EXPECT_NEAR(out[0], cel::earth::A + 100.0, 1e-6);
  EXPECT_NEAR(out[1], 0.0, 1e-6);

  fr::rotateVectorInto(x, EAST, out); // east -> +Y
  EXPECT_NEAR(out[1], 100.0, 1e-9);
  fr::rotateVectorInto(x, NORTH, out); // north -> +Z
  EXPECT_NEAR(out[2], 100.0, 1e-9);
}

/** @test NED and ENU at a site agree under (n,e,d) = (y_enu, x_enu, -z_enu). */
TEST(CatalogSiteTest, NedEnuConsistency) {
  const double LAT = 0.7, LON = -1.9, H = 350.0;
  fr::Transform<double> enu, ned;
  fr::enuSiteEdgeInto(LAT, LON, H, enu);
  fr::nedSiteEdgeInto(LAT, LON, H, ned);

  const double V_ENU[3] = {3.0, -4.0, 5.0};  // (e, n, u)
  const double V_NED[3] = {-4.0, 3.0, -5.0}; // (n, e, d) of the same vector
  double a[3], b[3];
  fr::rotateVectorInto(enu, V_ENU, a);
  fr::rotateVectorInto(ned, V_NED, b);
  for (int i = 0; i < 3; ++i) {
    EXPECT_NEAR(a[i], b[i], 1e-9);
  }
  // Same site origin.
  for (int i = 0; i < 3; ++i) {
    EXPECT_NEAR(enu.t[i], ned.t[i], 1e-9);
  }
}

/* -------------------------------- Catalog --------------------------------- */

/** @test The built catalog rotates ECEF under ECI at the GMST rate and keeps
 *        the two trees disconnected. */
TEST(CatalogBuildTest, EarthRotationAndTreeSeparation) {
  fr::Epoch epoch;
  epoch.init(cel::JD_J2000);
  fr::FrameGraph<double> g;
  fr::CatalogIds ids;
  ASSERT_EQ(fr::buildCatalog(g, epoch, ids), 0);
  ASSERT_EQ(g.size(), 4u);
  EXPECT_EQ(ids.hci, fr::K_NO_FRAME); // reserved, no edge yet

  // A point fixed on the ECEF +X axis, seen from ECI, advances by omega*t.
  const double P[3] = {cel::earth::A, 0.0, 0.0};
  double p0[3], p1[3];
  ASSERT_EQ(g.in(ids.eci).from(ids.ecef).point(P, p0, 0.0), 0);
  const double DT = 3600.0;
  ASSERT_EQ(g.in(ids.eci).from(ids.ecef).point(P, p1, DT), 0);
  const double ANG0 = std::atan2(p0[1], p0[0]);
  const double ANG1 = std::atan2(p1[1], p1[0]);
  double dAng = ANG1 - ANG0;
  if (dAng < 0) {
    dAng += apex::math::vecmat::TWO_PI;
  }
  EXPECT_NEAR(dAng, cel::earth::OMEGA * DT, 1e-9);

  // One model rotation period returns the point to its start (closure).
  double pDay[3];
  ASSERT_EQ(g.in(ids.eci).from(ids.ecef).point(P, pDay, K_ROTATION_PERIOD_S), 0);
  EXPECT_NEAR(pDay[0], p0[0], 1e-3); // mm-level closure over 86,164 s
  EXPECT_NEAR(pDay[1], p0[1], 1e-3);

  // Earth tree and moon tree do not resolve into each other.
  fr::Transform<double> x;
  EXPECT_EQ(g.resolve(ids.ecef, ids.mcmf, 0.0, x), static_cast<uint8_t>(fr::Status::ERROR_NO_PATH));
}

/** @test A site + catalog chain: ENU point through ECEF into ECI. */
TEST(CatalogBuildTest, SiteChainThroughCatalog) {
  fr::Epoch epoch;
  epoch.init(cel::JD_J2000);
  fr::FrameGraph<double> g;
  fr::CatalogIds ids;
  ASSERT_EQ(fr::buildCatalog(g, epoch, ids), 0);

  fr::Transform<double> siteEdge;
  fr::enuSiteEdgeInto(0.7, -1.9, 350.0, siteEdge);
  fr::FrameId site = 0;
  ASSERT_EQ(g.addStatic(ids.ecef, siteEdge, "site_enu", site), 0);

  // The site origin, expressed in ECI at t=0, equals the geodetic->ECEF
  // position rotated by theta0.
  const double ORIGIN[3] = {0, 0, 0};
  double pEci[3], pEcef[3];
  ASSERT_EQ(g.in(ids.eci).from(site).point(ORIGIN, pEci, 0.0), 0);
  fr::geodeticToEcefInto(0.7, -1.9, 350.0, pEcef);
  fr::Transform<double> spin;
  spin.rotation().setFromAngleAxis(epoch.thetaEarth0, 0.0, 0.0, 1.0);
  double want[3];
  fr::transformPointInto(spin, pEcef, want);
  for (int i = 0; i < 3; ++i) {
    EXPECT_NEAR(pEci[i], want[i], 1e-5);
  }
}

/** @test The moon tree rotates at the tidally locked rate. */
TEST(CatalogBuildTest, MoonRotation) {
  fr::Epoch epoch;
  epoch.init(cel::JD_J2000, 0.25); // arbitrary lunar meridian anchor
  fr::FrameGraph<double> g;
  fr::CatalogIds ids;
  ASSERT_EQ(fr::buildCatalog(g, epoch, ids), 0);

  const double P[3] = {1737400.0, 0.0, 0.0};
  double p0[3], p1[3];
  ASSERT_EQ(g.in(ids.mci).from(ids.mcmf).point(P, p0, 0.0), 0);
  ASSERT_EQ(g.in(ids.mci).from(ids.mcmf).point(P, p1, 86400.0), 0);
  const double D_ANG = std::atan2(p1[1], p1[0]) - std::atan2(p0[1], p0[0]);
  EXPECT_NEAR(D_ANG, apex::math::celestial::moon::OMEGA * 86400.0, 1e-9);
}

/** @test buildCatalog propagates capacity exhaustion mid-build. */
TEST(CatalogBuildTest, CapacityErrorPropagates) {
  fr::Epoch epoch;
  epoch.init(apex::math::celestial::JD_J2000);
  fr::FrameGraph<double, 3> g; // room for eci + ecef + mci, not mcmf
  fr::CatalogIds ids;
  EXPECT_EQ(fr::buildCatalog(g, epoch, ids), static_cast<uint8_t>(fr::Status::ERROR_CAPACITY));

  fr::FrameGraph<double, 2> two; // fails at the mci root
  fr::CatalogIds ids2;
  EXPECT_EQ(fr::buildCatalog(two, epoch, ids2), static_cast<uint8_t>(fr::Status::ERROR_CAPACITY));

  fr::FrameGraph<double, 1> tiny; // fails at the ecef edge
  fr::CatalogIds ids3;
  EXPECT_EQ(fr::buildCatalog(tiny, epoch, ids3), static_cast<uint8_t>(fr::Status::ERROR_CAPACITY));
}

/** @test An epoch before J2000 wraps its sidereal angle into [0, 2*pi). */
TEST(CatalogEpochTest, PreJ2000EpochWrapsPositive) {
  fr::Epoch e;
  e.init(apex::math::celestial::JD_J2000 - 3652.5); // ~a decade earlier
  EXPECT_GE(e.thetaEarth0, 0.0);
  EXPECT_LT(e.thetaEarth0, apex::math::vecmat::TWO_PI);
}
