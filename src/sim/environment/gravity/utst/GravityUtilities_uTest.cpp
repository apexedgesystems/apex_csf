/**
 * @file GravityUtilities_uTest.cpp
 * @brief Unit tests for gravity utility models and coordinate transforms.
 *
 * Tests:
 *  - J2GravityModel: Fast J2-only gravity
 *  - ZonalGravityModel: Zonal harmonics only
 *  - Geodetic: Coordinate conversions, normal gravity
 *  - GeoidModel: Geoid undulation computation
 */

#include "src/sim/environment/gravity/inc/CoeffSource.hpp"
#include "src/sim/environment/gravity/inc/J2GravityModel.hpp"
#include "src/sim/environment/gravity/inc/earth/ZonalGravityModel.hpp"
#include "src/sim/environment/gravity/inc/earth/Geodetic.hpp"
#include "src/sim/environment/gravity/inc/earth/GeoidModel.hpp"
#include "src/sim/environment/gravity/inc/earth/Wgs84Constants.hpp"

#include <cmath>

#include <gtest/gtest.h>

using sim::environment::gravity::CoeffSource;
using sim::environment::gravity::degToRad;
using sim::environment::gravity::ecefToGeodetic;
using sim::environment::gravity::ecefToNed;
using sim::environment::gravity::GeodeticCoord;
using sim::environment::gravity::geodeticToEcef;
using sim::environment::gravity::GeoidModel;
using sim::environment::gravity::GeoidParams;
using sim::environment::gravity::J2GravityModel;
using sim::environment::gravity::J2Params;
using sim::environment::gravity::nedToEcef;
using sim::environment::gravity::normalGravity;
using sim::environment::gravity::radToDeg;
using sim::environment::gravity::ZonalGravityModel;
using sim::environment::gravity::ZonalParams;
namespace egm2008 = sim::environment::gravity::egm2008;
namespace wgs84 = sim::environment::gravity::wgs84;

/* ----------------------------- Test Fixtures ----------------------------- */

/// Tiny source with N=0 and N=2 for basic model tests.
class TinySourceN2 final : public CoeffSource {
public:
  int16_t minDegree() const noexcept override { return 0; }
  int16_t maxDegree() const noexcept override { return 2; }
  bool get(int16_t n, int16_t m, double& C, double& S) const noexcept override {
    S = 0.0;
    if (n == 0 && m == 0) {
      C = 1.0;
      return true;
    }
    if (n == 2 && m == 0) {
      C = egm2008::C20; // Use EGM2008 J2 coefficient
      return true;
    }
    C = 0.0;
    return true;
  }
};

/* ----------------------------- WGS84 Constants Tests ----------------------------- */

/** @test WGS84 constants are physically reasonable. */
TEST(Wgs84ConstantsTest, ConstantsAreReasonable) {
  // Semi-major axis
  EXPECT_DOUBLE_EQ(wgs84::A, 6378137.0);

  // Flattening
  EXPECT_NEAR(wgs84::F, 1.0 / 298.257223563, 1e-15);

  // GM
  EXPECT_DOUBLE_EQ(wgs84::GM, 3.986004418e14);

  // Derived: b = a(1-f)
  EXPECT_NEAR(wgs84::B, wgs84::A * (1.0 - wgs84::F), 1.0);

  // Eccentricity squared should be positive and small
  EXPECT_GT(wgs84::E2, 0.0);
  EXPECT_LT(wgs84::E2, 0.01);
}

/** @test EGM2008 J2 is positive and has expected magnitude. */
TEST(Wgs84ConstantsTest, EGM2008J2Value) {
  // C20 is negative (Earth is oblate)
  EXPECT_LT(egm2008::C20, 0.0);

  // J2 is positive by convention: J2 = -sqrt(5) * C20
  EXPECT_GT(egm2008::J2, 0.0);

  // J2 should be approximately 1.082e-3
  EXPECT_NEAR(egm2008::J2, 1.0826e-3, 1e-6);

  // C20 should be approximately -0.484e-3
  EXPECT_NEAR(egm2008::C20, -0.484e-3, 1e-6);
}

/* ----------------------------- J2 Model Tests ----------------------------- */

/** @test J2 model reduces to central body at large distance. */
TEST(J2GravityModelTest, ReducesToCentralAtLargeDistance) {
  J2GravityModel model;
  J2Params params{wgs84::GM, wgs84::A, egm2008::J2};
  ASSERT_TRUE(model.init(params));

  // At 100 Earth radii, J2 effect is negligible
  const double FAR_R = 100.0 * wgs84::A;
  const double R[3] = {FAR_R, 0.0, 0.0};

  double V = 0.0;
  ASSERT_TRUE(model.potential(R, V));

  const double V_CENTRAL = wgs84::GM / FAR_R;
  EXPECT_NEAR(V, V_CENTRAL, V_CENTRAL * 1e-6); // Within 1 ppm
}

/** @test J2 model shows latitude dependence at same radius. */
TEST(J2GravityModelTest, LatitudeDependence) {
  J2GravityModel model;
  J2Params params{wgs84::GM, wgs84::A, egm2008::J2};
  ASSERT_TRUE(model.init(params));

  const double R_SURFACE = wgs84::A;
  const double R_EQ[3] = {R_SURFACE, 0.0, 0.0};
  const double R_POL[3] = {0.0, 0.0, R_SURFACE};

  double aEq[3] = {}, aPol[3] = {};
  ASSERT_TRUE(model.acceleration(R_EQ, aEq));
  ASSERT_TRUE(model.acceleration(R_POL, aPol));

  const double MAG_EQ = std::sqrt(aEq[0] * aEq[0] + aEq[1] * aEq[1] + aEq[2] * aEq[2]);
  const double MAG_POL = std::sqrt(aPol[0] * aPol[0] + aPol[1] * aPol[1] + aPol[2] * aPol[2]);

  // At SAME radius, J2 makes equatorial gravity HIGHER than polar gravity.
  // (Real-world polar > equator is due to: (1) oblate shape, (2) centrifugal force)
  EXPECT_GT(MAG_EQ, MAG_POL);

  // Difference should be around 0.5% for J2
  const double DIFF_PERCENT = (MAG_EQ - MAG_POL) / MAG_POL * 100.0;
  EXPECT_GT(DIFF_PERCENT, 0.3);
  EXPECT_LT(DIFF_PERCENT, 1.0);
}

/** @test J2 model is longitude-invariant (zonal). */
TEST(J2GravityModelTest, LongitudeInvariant) {
  J2GravityModel model;
  J2Params params{wgs84::GM, wgs84::A, egm2008::J2};
  ASSERT_TRUE(model.init(params));

  const double R1[3] = {wgs84::A, 0.0, 0.0};
  const double R2[3] = {0.0, wgs84::A, 0.0};
  const double R3[3] = {wgs84::A / std::sqrt(2.0), wgs84::A / std::sqrt(2.0), 0.0};

  double V1 = 0.0, V2 = 0.0, V3 = 0.0;
  ASSERT_TRUE(model.potential(R1, V1));
  ASSERT_TRUE(model.potential(R2, V2));
  ASSERT_TRUE(model.potential(R3, V3));

  // Use relative tolerance for large values
  EXPECT_NEAR(V1, V2, std::abs(V1) * 1e-14);
  EXPECT_NEAR(V1, V3, std::abs(V1) * 1e-14);
}

/** @test J2 model reports max degree = 2. */
TEST(J2GravityModelTest, MaxDegreeIsTwo) {
  J2GravityModel model;
  J2Params params{wgs84::GM, wgs84::A, egm2008::J2};
  ASSERT_TRUE(model.init(params));
  EXPECT_EQ(model.maxDegree(), 2);
}

/* ----------------------------- Zonal Model Tests ----------------------------- */

/** @test Zonal model with N=2 matches J2 model. */
TEST(ZonalGravityModelTest, N2MatchesJ2Model) {
  J2GravityModel j2Model;
  J2Params j2params{wgs84::GM, wgs84::A, egm2008::J2};
  ASSERT_TRUE(j2Model.init(j2params));

  ZonalGravityModel zonalModel;
  ZonalParams params;
  params.N = 2;
  ASSERT_TRUE(zonalModel.init(params));

  const double R[3] = {7000e3, 0.0, 0.0};

  double vJ2 = 0.0, vZonal = 0.0;
  ASSERT_TRUE(j2Model.potential(R, vJ2));
  ASSERT_TRUE(zonalModel.potential(R, vZonal));

  // Should match closely
  EXPECT_NEAR(vJ2, vZonal, std::abs(vJ2) * 1e-8);

  double aJ2[3] = {}, aZonal[3] = {};
  ASSERT_TRUE(j2Model.acceleration(R, aJ2));
  ASSERT_TRUE(zonalModel.acceleration(R, aZonal));

  EXPECT_NEAR(aJ2[0], aZonal[0], std::abs(aJ2[0]) * 1e-6);
  EXPECT_NEAR(aJ2[1], aZonal[1], 1e-10);
  EXPECT_NEAR(aJ2[2], aZonal[2], 1e-10);
}

/** @test Zonal model is longitude-invariant. */
TEST(ZonalGravityModelTest, LongitudeInvariant) {
  ZonalGravityModel model;
  ZonalParams params;
  params.N = 10;
  ASSERT_TRUE(model.init(params));

  const double R1[3] = {wgs84::A, 0.0, 0.0};
  const double R2[3] = {0.0, wgs84::A, 0.0};

  double V1 = 0.0, V2 = 0.0;
  ASSERT_TRUE(model.potential(R1, V1));
  ASSERT_TRUE(model.potential(R2, V2));

  EXPECT_NEAR(V1, V2, 1e-10);
}

/** @test Higher degree zonal gives more refined result. */
TEST(ZonalGravityModelTest, HigherDegreeRefinement) {
  ZonalGravityModel model2, model10;
  ZonalParams p2, p10;
  p2.N = 2;
  p10.N = 10;
  ASSERT_TRUE(model2.init(p2));
  ASSERT_TRUE(model10.init(p10));

  const double R[3] = {wgs84::A + 400e3, 0.0, 0.0}; // LEO altitude

  double V2 = 0.0, V10 = 0.0;
  ASSERT_TRUE(model2.potential(R, V2));
  ASSERT_TRUE(model10.potential(R, V10));

  // Values should be close but not identical
  EXPECT_NEAR(V2, V10, std::abs(V2) * 0.001); // Within 0.1%

  // They should differ due to higher harmonics
  EXPECT_NE(V2, V10);
}

/* ----------------------------- Geodetic Tests ----------------------------- */

/** @test Round-trip ECEF <-> Geodetic preserves position. */
TEST(GeodeticTest, RoundTripEcefGeodetic) {
  // Test at various latitudes
  const double LATS[] = {0.0, 45.0, 89.0, -30.0, -85.0};
  const double LONS[] = {0.0, 90.0, -120.0, 180.0, 45.0};
  const double ALTS[] = {0.0, 1000.0, 400e3, -100.0, 35786e3}; // MSL to GEO

  for (double latDeg : LATS) {
    for (double lonDeg : LONS) {
      for (double alt : ALTS) {
        const double LAT = degToRad(latDeg);
        const double LON = degToRad(lonDeg);

        // Geodetic -> ECEF
        double ecef[3] = {};
        geodeticToEcef(LAT, LON, alt, ecef);

        // ECEF -> Geodetic
        GeodeticCoord geo{};
        ecefToGeodetic(ecef, geo);

        EXPECT_NEAR(geo.lat, LAT, 1e-10) << "lat=" << latDeg << " lon=" << lonDeg << " alt=" << alt;
        EXPECT_NEAR(geo.lon, LON, 1e-10) << "lat=" << latDeg << " lon=" << lonDeg << " alt=" << alt;
        // Altitude accuracy ~0.1mm (sub-millimeter) for all altitudes
        EXPECT_NEAR(geo.alt, alt, 1e-4) << "lat=" << latDeg << " lon=" << lonDeg << " alt=" << alt;
      }
    }
  }
}

/** @test Equator at prime meridian maps to +X axis. */
TEST(GeodeticTest, EquatorPrimeMeridian) {
  double ecef[3] = {};
  geodeticToEcef(0.0, 0.0, 0.0, ecef);

  EXPECT_NEAR(ecef[0], wgs84::A, 1e-6);
  EXPECT_NEAR(ecef[1], 0.0, 1e-10);
  EXPECT_NEAR(ecef[2], 0.0, 1e-10);
}

/** @test North pole maps to +Z axis. */
TEST(GeodeticTest, NorthPole) {
  const double LAT_90 = degToRad(90.0);
  double ecef[3] = {};
  geodeticToEcef(LAT_90, 0.0, 0.0, ecef);

  EXPECT_NEAR(ecef[0], 0.0, 1e-6);
  EXPECT_NEAR(ecef[1], 0.0, 1e-6);
  // Polar radius with sub-mm tolerance (WGS84 B vs computed differ slightly)
  EXPECT_NEAR(ecef[2], wgs84::B, 1e-4);
}

/** @test Normal gravity follows Somigliana formula properties. */
TEST(GeodeticTest, NormalGravityProperties) {
  // Equator gravity
  const double GAMMA_EQ = normalGravity(0.0, 0.0);

  // Pole gravity
  const double GAMMA_POL = normalGravity(degToRad(90.0), 0.0);

  // Pole gravity > Equator gravity (due to shape and rotation)
  EXPECT_GT(GAMMA_POL, GAMMA_EQ);

  // Both should be around 9.8 m/s^2
  EXPECT_GT(GAMMA_EQ, 9.7);
  EXPECT_LT(GAMMA_EQ, 10.0);
  EXPECT_GT(GAMMA_POL, 9.8);
  EXPECT_LT(GAMMA_POL, 10.0);

  // Difference should be about 0.5%
  const double DIFF = (GAMMA_POL - GAMMA_EQ) / GAMMA_EQ * 100.0;
  EXPECT_GT(DIFF, 0.4);
  EXPECT_LT(DIFF, 0.6);
}

/** @test Normal gravity decreases with altitude. */
TEST(GeodeticTest, NormalGravityAltitude) {
  const double GAMMA_SURFACE = normalGravity(degToRad(45.0), 0.0);
  const double GAMMA_LEO = normalGravity(degToRad(45.0), 400e3);
  const double GAMMA_GEO = normalGravity(degToRad(45.0), 35786e3);

  EXPECT_GT(GAMMA_SURFACE, GAMMA_LEO);
  EXPECT_GT(GAMMA_LEO, GAMMA_GEO);

  // LEO should be ~90% of surface
  EXPECT_GT(GAMMA_LEO / GAMMA_SURFACE, 0.85);
  EXPECT_LT(GAMMA_LEO / GAMMA_SURFACE, 0.95);
}

/** @test NED frame properties. */
TEST(GeodeticTest, NedFrameOrthogonality) {
  const double LAT = degToRad(45.0);
  const double LON = degToRad(90.0);

  // Get position in ECEF
  double ecef[3] = {};
  geodeticToEcef(LAT, LON, 0.0, ecef);

  // Transform unit vectors to ECEF
  const double N_NED[3] = {1.0, 0.0, 0.0};
  const double E_NED[3] = {0.0, 1.0, 0.0};
  const double D_NED[3] = {0.0, 0.0, 1.0};

  double nEcef[3] = {}, eEcef[3] = {}, dEcef[3] = {};
  nedToEcef(LAT, LON, N_NED, nEcef);
  nedToEcef(LAT, LON, E_NED, eEcef);
  nedToEcef(LAT, LON, D_NED, dEcef);

  // Check orthogonality: dot products should be zero
  const double DOT_NE = nEcef[0] * eEcef[0] + nEcef[1] * eEcef[1] + nEcef[2] * eEcef[2];
  const double DOT_ND = nEcef[0] * dEcef[0] + nEcef[1] * dEcef[1] + nEcef[2] * dEcef[2];
  const double DOT_ED = eEcef[0] * dEcef[0] + eEcef[1] * dEcef[1] + eEcef[2] * dEcef[2];

  EXPECT_NEAR(DOT_NE, 0.0, 1e-15);
  EXPECT_NEAR(DOT_ND, 0.0, 1e-15);
  EXPECT_NEAR(DOT_ED, 0.0, 1e-15);

  // Check unit length
  const double LEN_N = std::sqrt(nEcef[0] * nEcef[0] + nEcef[1] * nEcef[1] + nEcef[2] * nEcef[2]);
  const double LEN_E = std::sqrt(eEcef[0] * eEcef[0] + eEcef[1] * eEcef[1] + eEcef[2] * eEcef[2]);
  const double LEN_D = std::sqrt(dEcef[0] * dEcef[0] + dEcef[1] * dEcef[1] + dEcef[2] * dEcef[2]);

  EXPECT_NEAR(LEN_N, 1.0, 1e-15);
  EXPECT_NEAR(LEN_E, 1.0, 1e-15);
  EXPECT_NEAR(LEN_D, 1.0, 1e-15);
}

/** @test NED round-trip preserves vector. */
TEST(GeodeticTest, NedRoundTrip) {
  const double LAT = degToRad(30.0);
  const double LON = degToRad(-45.0);
  const double V_NED[3] = {100.0, -50.0, 25.0};

  double vEcef[3] = {};
  nedToEcef(LAT, LON, V_NED, vEcef);

  double vNed2[3] = {};
  ecefToNed(LAT, LON, vEcef, vNed2);

  EXPECT_NEAR(vNed2[0], V_NED[0], 1e-10);
  EXPECT_NEAR(vNed2[1], V_NED[1], 1e-10);
  EXPECT_NEAR(vNed2[2], V_NED[2], 1e-10);
}

/* ----------------------------- GeoidModel Tests ----------------------------- */

/** @test GeoidModel initialization. */
TEST(GeoidModelTest, Initialization) {
  TinySourceN2 src;
  GeoidModel model;
  GeoidParams params;
  params.N = 2;

  ASSERT_TRUE(model.init(src, params));
  EXPECT_EQ(model.maxDegree(), 2);
}

/** @test Geoid undulation is finite and varies with latitude. */
TEST(GeoidModelTest, UndulationProperties) {
  TinySourceN2 src;
  GeoidModel model;
  GeoidParams params;
  params.N = 2;
  ASSERT_TRUE(model.init(src, params));

  // Test at various locations - undulation should be finite and non-NaN
  const double LATS[] = {0.0, 45.0, 89.0, -45.0, -89.0};
  const double LONS[] = {0.0, 90.0, 180.0, -90.0};

  for (double latDeg : LATS) {
    for (double lonDeg : LONS) {
      const double N = model.undulation(degToRad(latDeg), degToRad(lonDeg));

      // Undulation should be finite (not NaN or Inf)
      EXPECT_TRUE(std::isfinite(N)) << "lat=" << latDeg << " lon=" << lonDeg << " N=" << N;
    }
  }

  // Zonal model: undulation should be longitude-invariant
  const double N1 = model.undulation(degToRad(45.0), degToRad(0.0));
  const double N2 = model.undulation(degToRad(45.0), degToRad(90.0));
  EXPECT_NEAR(N1, N2, std::abs(N1) * 1e-10);

  // Undulation should vary with latitude (J2 effect)
  const double N_EQ = model.undulation(degToRad(0.0), degToRad(0.0));
  const double N_45 = model.undulation(degToRad(45.0), degToRad(0.0));
  EXPECT_NE(N_EQ, N_45);
}

/** @test Height conversion round-trip. */
TEST(GeoidModelTest, HeightConversionRoundTrip) {
  TinySourceN2 src;
  GeoidModel model;
  GeoidParams params;
  params.N = 2;
  ASSERT_TRUE(model.init(src, params));

  const double LAT = degToRad(45.0);
  const double LON = degToRad(-75.0);
  const double H_ELLIPSOID = 1000.0; // Height above ellipsoid

  // Convert to orthometric
  const double H_ORTHO = model.ellipsoidToOrthometric(LAT, LON, H_ELLIPSOID);

  // Convert back
  const double H_ELLIPSOID2 = model.orthometricToEllipsoid(LAT, LON, H_ORTHO);

  EXPECT_NEAR(H_ELLIPSOID2, H_ELLIPSOID, 1e-10);
}

/* ----------------------------- Angle Conversion Tests ----------------------------- */

/** @test Degree to radian conversion. */
TEST(AngleConversionTest, DegToRad) {
  EXPECT_NEAR(degToRad(0.0), 0.0, 1e-15);
  EXPECT_NEAR(degToRad(90.0), M_PI / 2.0, 1e-15);
  EXPECT_NEAR(degToRad(180.0), M_PI, 1e-15);
  EXPECT_NEAR(degToRad(360.0), 2.0 * M_PI, 1e-15);
  EXPECT_NEAR(degToRad(-90.0), -M_PI / 2.0, 1e-15);
}

/** @test Radian to degree conversion. */
TEST(AngleConversionTest, RadToDeg) {
  EXPECT_NEAR(radToDeg(0.0), 0.0, 1e-15);
  EXPECT_NEAR(radToDeg(M_PI / 2.0), 90.0, 1e-15);
  EXPECT_NEAR(radToDeg(M_PI), 180.0, 1e-15);
  EXPECT_NEAR(radToDeg(2.0 * M_PI), 360.0, 1e-15);
  EXPECT_NEAR(radToDeg(-M_PI / 2.0), -90.0, 1e-15);
}

/** @test Round-trip angle conversion. */
TEST(AngleConversionTest, RoundTrip) {
  const double ANGLES_DEG[] = {0.0, 30.0, 45.0, 60.0, 90.0, 180.0, 270.0, 360.0, -45.0, -180.0};
  for (double deg : ANGLES_DEG) {
    EXPECT_NEAR(radToDeg(degToRad(deg)), deg, 1e-12) << "deg=" << deg;
  }
}
