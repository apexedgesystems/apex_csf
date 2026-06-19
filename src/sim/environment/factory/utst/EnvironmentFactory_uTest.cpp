/**
 * @file EnvironmentFactory_uTest.cpp
 * @brief Tests for the cross-subsystem gravity + terrain + atmosphere factory.
 *
 * Coverage:
 *   - Every (Body, *Fidelity) dispatch path returns the model the contract
 *     promises (non-null shell, or nullptr for the reserved EMPIRICAL slot).
 *   - Analytic models are returned READY (default-init produces sensible
 *     queries without further setup).
 *   - File-backed models (gravity SPHERICAL, terrain HTILE for OTHER,
 *     atmosphere LAYERED for OTHER) are returned uninitialized -- the caller
 *     has to do the load/init.
 *   - HTILE for EARTH / MOON returns the typed wrapper (SrtmTerrainModel /
 *     LolaTerrainModel); LAYERED for EARTH / MOON returns the typed wrapper
 *     (Ussa76AtmosphereModel / VacuumAtmosphereModel).
 *   - The bundled makeEnvironment path and the EnvironmentSpec defaults.
 *   - The Body / *Fidelity pretty-printers, including the UNKNOWN fall-through
 *     arms reached via an out-of-range cast.
 *
 * Terrain and atmosphere queries return their respective `Status` enums (not
 * bool); this test consumes them through the published `isSuccess` /
 * `isWarning` / `isError` helpers. Gravity still returns bool.
 */

#include "src/sim/environment/atmosphere/inc/AtmosphereStatus.hpp"
#include "src/sim/environment/atmosphere/inc/earth/Ussa76AtmosphereModel.hpp"
#include "src/sim/environment/atmosphere/inc/moon/VacuumAtmosphereModel.hpp"
#include "src/sim/environment/factory/inc/Body.hpp"
#include "src/sim/environment/factory/inc/EnvironmentFactory.hpp"
#include "src/sim/environment/factory/inc/EnvironmentFidelity.hpp"
#include "src/sim/environment/terrain/inc/TerrainStatus.hpp"
#include "src/sim/environment/terrain/inc/earth/SrtmTerrainModel.hpp"
#include "src/sim/environment/terrain/inc/moon/LolaTerrainModel.hpp"

#include <gtest/gtest.h>

#include <cmath>

using sim::environment::AtmosphereFidelity;
using sim::environment::Body;
using sim::environment::EnvironmentModels;
using sim::environment::GravityFidelity;
using sim::environment::makeAtmosphereModel;
using sim::environment::makeEnvironment;
using sim::environment::makeGravityModel;
using sim::environment::makeTerrainModel;
using sim::environment::TerrainFidelity;

namespace atm = sim::environment::atmosphere;
namespace ter = sim::environment::terrain;

namespace {

/* ----------------------------- Gravity factory ----------------------------- */

TEST(EnvironmentFactory, MakesConstantGravityForAllBodies) {
  for (Body b : {Body::EARTH, Body::MOON, Body::OTHER}) {
    auto g = makeGravityModel(b, GravityFidelity::CONSTANT);
    ASSERT_NE(g, nullptr) << "body=" << sim::environment::toString(b);
    // Constant model is RT-safe ready-to-use: query at any position.
    const double R[3] = {7e6, 0.0, 0.0};
    double a[3] = {0.0, 0.0, 0.0};
    EXPECT_TRUE(g->acceleration(R, a));
    EXPECT_GT(std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]), 0.0);
  }
}

TEST(EnvironmentFactory, ConstantGravityUsesBodySpecificSurfaceGravity) {
  // Earth and Moon constant gravity differ in magnitude; Earth is stronger.
  auto earth = makeGravityModel(Body::EARTH, GravityFidelity::CONSTANT);
  auto moon = makeGravityModel(Body::MOON, GravityFidelity::CONSTANT);
  ASSERT_NE(earth, nullptr);
  ASSERT_NE(moon, nullptr);
  const double R[3] = {7e6, 0.0, 0.0};
  double ae[3] = {0.0, 0.0, 0.0};
  double am[3] = {0.0, 0.0, 0.0};
  ASSERT_TRUE(earth->acceleration(R, ae));
  ASSERT_TRUE(moon->acceleration(R, am));
  const double me = std::sqrt(ae[0] * ae[0] + ae[1] * ae[1] + ae[2] * ae[2]);
  const double mm = std::sqrt(am[0] * am[0] + am[1] * am[1] + am[2] * am[2]);
  EXPECT_GT(me, mm) << "Earth surface gravity should exceed the Moon's";
}

TEST(EnvironmentFactory, MakesJ2GravityForKnownBodies) {
  // Earth + Moon should be init'd with body-specific GM/a/J2.
  for (Body b : {Body::EARTH, Body::MOON}) {
    auto g = makeGravityModel(b, GravityFidelity::J2);
    ASSERT_NE(g, nullptr) << "body=" << sim::environment::toString(b);
    const double R[3] = {7e6, 0.0, 0.0};
    double a[3] = {0.0, 0.0, 0.0};
    // J2 model returns sensible acceleration on Earth/Moon defaults.
    EXPECT_TRUE(g->acceleration(R, a));
    EXPECT_GT(std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]), 0.0);
  }
}

TEST(EnvironmentFactory, MakesJ2ForOtherButUninitialized) {
  // Body::OTHER -> J2 returns uninitialized; acceleration may produce
  // zero or NaN. We just verify the pointer is non-null and the call
  // doesn't crash.
  auto g = makeGravityModel(Body::OTHER, GravityFidelity::J2);
  ASSERT_NE(g, nullptr);
}

TEST(EnvironmentFactory, MakesSphericalGravityUninitialized) {
  // SphericalHarmonicModel needs a CoeffSource; factory returns the shell for
  // every body. Caller must wire up data.
  for (Body b : {Body::EARTH, Body::MOON, Body::OTHER}) {
    auto g = makeGravityModel(b, GravityFidelity::SPHERICAL);
    ASSERT_NE(g, nullptr) << "body=" << sim::environment::toString(b);
  }
}

/* ----------------------------- Terrain factory ----------------------------- */

TEST(EnvironmentFactory, MakesConstantTerrainForAllBodies) {
  for (Body b : {Body::EARTH, Body::MOON, Body::OTHER}) {
    auto t = makeTerrainModel(b, TerrainFidelity::CONSTANT);
    ASSERT_NE(t, nullptr);
    double H = 999.0;
    EXPECT_TRUE(ter::isSuccess(t->elevationAt(0.0, 0.0, H)));
    EXPECT_DOUBLE_EQ(H, 0.0);
  }
}

TEST(EnvironmentFactory, MakesSphereTerrainWithBodyRadius) {
  auto earth = makeTerrainModel(Body::EARTH, TerrainFidelity::SPHERE);
  auto moon = makeTerrainModel(Body::MOON, TerrainFidelity::SPHERE);
  ASSERT_NE(earth, nullptr);
  ASSERT_NE(moon, nullptr);
  // ECEF query at the body's radius should give H ~= 0.
  const double EARTH_EC[3] = {6378137.0, 0.0, 0.0};
  const double MOON_EC[3] = {1737400.0, 0.0, 0.0};
  double H = -999.0;
  ASSERT_TRUE(ter::isSuccess(earth->elevationAtEcef(EARTH_EC, H)));
  EXPECT_NEAR(H, 0.0, 1e-3);
  ASSERT_TRUE(ter::isSuccess(moon->elevationAtEcef(MOON_EC, H)));
  EXPECT_NEAR(H, 0.0, 1e-3);
}

TEST(EnvironmentFactory, MakesSphereTerrainForOther) {
  // OTHER has no body radius default (unit reference radius); the model is
  // still returned so callers can query or re-parameterize it.
  auto t = makeTerrainModel(Body::OTHER, TerrainFidelity::SPHERE);
  ASSERT_NE(t, nullptr);
  double H = -999.0;
  const double EC[3] = {1.0, 0.0, 0.0};
  EXPECT_TRUE(ter::isSuccess(t->elevationAtEcef(EC, H)));
}

TEST(EnvironmentFactory, MakesEllipsoidTerrainForEarth) {
  auto t = makeTerrainModel(Body::EARTH, TerrainFidelity::ELLIPSOID);
  ASSERT_NE(t, nullptr);
  // ECEF at +x equator: H = 0 (on the ellipsoid).
  const double EC[3] = {6378137.0, 0.0, 0.0};
  double H = -999.0;
  ASSERT_TRUE(ter::isSuccess(t->elevationAtEcef(EC, H)));
  EXPECT_NEAR(H, 0.0, 1e-3);
}

TEST(EnvironmentFactory, MakesEllipsoidTerrainForMoonAndOther) {
  // Moon is treated as a sphere (rEq == rPol); OTHER uses unit radii. Both
  // return a usable model.
  for (Body b : {Body::MOON, Body::OTHER}) {
    auto t = makeTerrainModel(b, TerrainFidelity::ELLIPSOID);
    ASSERT_NE(t, nullptr) << "body=" << sim::environment::toString(b);
  }
  auto moon = makeTerrainModel(Body::MOON, TerrainFidelity::ELLIPSOID);
  const double EC[3] = {1737400.0, 0.0, 0.0};
  double H = -999.0;
  ASSERT_TRUE(ter::isSuccess(moon->elevationAtEcef(EC, H)));
  EXPECT_NEAR(H, 0.0, 1e-3);
}

TEST(EnvironmentFactory, MakesHtileEarthAsSrtmModel) {
  // Verify the HTILE path returns the Earth-typed wrapper (so callers
  // get the loadEarth/isEarthValid affordances).
  auto t = makeTerrainModel(Body::EARTH, TerrainFidelity::HTILE);
  ASSERT_NE(t, nullptr);
  EXPECT_NE(dynamic_cast<ter::earth::SrtmTerrainModel*>(t.get()), nullptr)
      << "HTILE for EARTH should return SrtmTerrainModel";
}

TEST(EnvironmentFactory, MakesHtileMoonAsLolaModel) {
  auto t = makeTerrainModel(Body::MOON, TerrainFidelity::HTILE);
  ASSERT_NE(t, nullptr);
  EXPECT_NE(dynamic_cast<ter::moon::LolaTerrainModel*>(t.get()), nullptr)
      << "HTILE for MOON should return LolaTerrainModel";
}

TEST(EnvironmentFactory, MakesHtileOtherAsGenericHtileTile) {
  // Body::OTHER falls back to generic HtileTile (not a body wrapper). It is
  // returned uninitialized, so a query before load() is an error.
  auto t = makeTerrainModel(Body::OTHER, TerrainFidelity::HTILE);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(dynamic_cast<ter::earth::SrtmTerrainModel*>(t.get()), nullptr);
  EXPECT_EQ(dynamic_cast<ter::moon::LolaTerrainModel*>(t.get()), nullptr);
  double H = 0.0;
  EXPECT_TRUE(ter::isError(t->elevationAt(0.0, 0.0, H)))
      << "an unloaded htile model should reject queries";
}

/* ----------------------------- Atmosphere factory ----------------------------- */

TEST(EnvironmentFactory, MakesConstantAtmosphereForAllBodies) {
  for (Body b : {Body::EARTH, Body::MOON, Body::OTHER}) {
    auto a = makeAtmosphereModel(b, AtmosphereFidelity::CONSTANT);
    ASSERT_NE(a, nullptr) << "body=" << sim::environment::toString(b);
    atm::AtmosphereState s;
    const atm::Status st = a->query(0.0, 0.0, 0.0, s);
    if (b == Body::EARTH) {
      // Earth defaults to sea-level ISA -- non-vacuum, valid sample.
      EXPECT_TRUE(atm::isSuccess(st));
      EXPECT_FALSE(a->isVacuum());
      EXPECT_GT(s.rho, 0.0);
    } else {
      // Moon + OTHER default to vacuum: a warn (not an error) with rho == 0.
      EXPECT_TRUE(atm::isWarning(st)) << "vacuum query should warn, not error";
      EXPECT_TRUE(a->isVacuum());
      EXPECT_DOUBLE_EQ(s.rho, 0.0);
    }
  }
}

TEST(EnvironmentFactory, MakesExponentialAtmosphereForEarth) {
  auto a = makeAtmosphereModel(Body::EARTH, AtmosphereFidelity::EXPONENTIAL);
  ASSERT_NE(a, nullptr);
  atm::AtmosphereState s0, sH;
  ASSERT_TRUE(atm::isSuccess(a->query(0.0, 0.0, 0.0, s0)));
  ASSERT_TRUE(atm::isSuccess(a->query(8500.0, 0.0, 0.0, sH)));
  // One scale height up: rho should drop by factor of e.
  EXPECT_NEAR(s0.rho / sH.rho, M_E, 1e-9);
}

TEST(EnvironmentFactory, MakesExponentialAtmosphereForMoonAsVacuum) {
  // Moon has no exponential atmosphere physically -- we return vacuum.
  auto a = makeAtmosphereModel(Body::MOON, AtmosphereFidelity::EXPONENTIAL);
  ASSERT_NE(a, nullptr);
  EXPECT_TRUE(a->isVacuum());
  EXPECT_NE(dynamic_cast<atm::moon::VacuumAtmosphereModel*>(a.get()), nullptr)
      << "EXPONENTIAL for MOON should return VacuumAtmosphereModel";
}

TEST(EnvironmentFactory, MakesExponentialAtmosphereForOther) {
  // OTHER falls back to a ready ExponentialAtmosphere with default Earth-tropo
  // params; it is not the Moon vacuum wrapper and is non-vacuum.
  auto a = makeAtmosphereModel(Body::OTHER, AtmosphereFidelity::EXPONENTIAL);
  ASSERT_NE(a, nullptr);
  EXPECT_FALSE(a->isVacuum());
  atm::AtmosphereState s;
  EXPECT_TRUE(atm::isSuccess(a->query(0.0, 0.0, 0.0, s)));
  EXPECT_GT(s.rho, 0.0);
}

TEST(EnvironmentFactory, MakesLayeredEarthAsUssa76) {
  auto a = makeAtmosphereModel(Body::EARTH, AtmosphereFidelity::LAYERED);
  ASSERT_NE(a, nullptr);
  EXPECT_NE(dynamic_cast<atm::earth::Ussa76AtmosphereModel*>(a.get()), nullptr)
      << "LAYERED for EARTH should return Ussa76AtmosphereModel";
  // Sanity-check sea-level USSA76 reference values.
  atm::AtmosphereState s;
  ASSERT_TRUE(atm::isSuccess(a->query(0.0, 0.0, 0.0, s)));
  EXPECT_NEAR(s.T, 288.15, 0.01);
  EXPECT_NEAR(s.P, 101325.0, 1.0);
  EXPECT_NEAR(s.rho, 1.225, 0.001);
}

TEST(EnvironmentFactory, MakesLayeredMoonAsVacuum) {
  auto a = makeAtmosphereModel(Body::MOON, AtmosphereFidelity::LAYERED);
  ASSERT_NE(a, nullptr);
  EXPECT_NE(dynamic_cast<atm::moon::VacuumAtmosphereModel*>(a.get()), nullptr)
      << "LAYERED for MOON should return VacuumAtmosphereModel";
  EXPECT_TRUE(a->isVacuum());
}

TEST(EnvironmentFactory, MakesLayeredOtherUninitialized) {
  // Body::OTHER falls back to a generic LayeredAtmosphere; caller calls load().
  auto a = makeAtmosphereModel(Body::OTHER, AtmosphereFidelity::LAYERED);
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(dynamic_cast<atm::earth::Ussa76AtmosphereModel*>(a.get()), nullptr);
  // Uninitialized layered model rejects queries with an error status.
  atm::AtmosphereState s;
  EXPECT_TRUE(atm::isError(a->query(0.0, 0.0, 0.0, s)));
}

TEST(EnvironmentFactory, MakesEmpiricalReturnsNullptr) {
  // EMPIRICAL slot reserved for NRLMSISE-00 et al; not implemented yet. This
  // is the factory's only failure mode: an unsupported fidelity returns
  // nullptr for every body.
  for (Body b : {Body::EARTH, Body::MOON, Body::OTHER}) {
    auto a = makeAtmosphereModel(b, AtmosphereFidelity::EMPIRICAL);
    EXPECT_EQ(a, nullptr) << "body=" << sim::environment::toString(b);
  }
}

/* ----------------------------- Unsupported / out-of-range ----------------------------- */

TEST(EnvironmentFactory, RejectsOutOfRangeFidelityWithNullptr) {
  // An enum value outside the defined ladder (e.g. a corrupted/forward-compat
  // config) must not dispatch to a model; the factory returns nullptr rather
  // than constructing a wrong or default model.
  const auto badGravity = static_cast<GravityFidelity>(200);
  const auto badTerrain = static_cast<TerrainFidelity>(200);
  const auto badAtmosphere = static_cast<AtmosphereFidelity>(200);
  EXPECT_EQ(makeGravityModel(Body::EARTH, badGravity), nullptr);
  EXPECT_EQ(makeTerrainModel(Body::EARTH, badTerrain), nullptr);
  EXPECT_EQ(makeAtmosphereModel(Body::EARTH, badAtmosphere), nullptr);
}

/* ----------------------------- Bundled ----------------------------- */

TEST(EnvironmentFactory, MakesEnvironmentBundle) {
  EnvironmentModels env = makeEnvironment(sim::environment::EnvironmentSpec{
      Body::EARTH, GravityFidelity::J2, TerrainFidelity::ELLIPSOID, AtmosphereFidelity::LAYERED});
  ASSERT_NE(env.gravity, nullptr);
  ASSERT_NE(env.terrain, nullptr);
  ASSERT_NE(env.atmosphere, nullptr);
  // Just confirm all are usable at a typical orbit point.
  const double R[3] = {6378137.0 + 4e5, 0.0, 0.0};
  double a[3] = {0.0, 0.0, 0.0};
  EXPECT_TRUE(env.gravity->acceleration(R, a));
  double H = 0.0;
  EXPECT_TRUE(ter::isSuccess(env.terrain->elevationAtEcef(R, H)));
  EXPECT_NEAR(H, 4e5, 1.0);
  // 400 km is well above the USSA76 documented range (86 km), so the query
  // returns a non-SUCCESS status -- we verify it does not crash and reports
  // a warning/error rather than a bogus SUCCESS.
  atm::AtmosphereState s;
  EXPECT_FALSE(atm::isSuccess(env.atmosphere->query(4e5, 0.0, 0.0, s)));
}

TEST(EnvironmentFactory, MakesEnvironmentBundlePassesEmpiricalNullThrough) {
  // A spec selecting the unimplemented EMPIRICAL atmosphere leaves the bundle's
  // atmosphere pointer null while still building the other subsystems.
  sim::environment::EnvironmentSpec spec{};
  spec.body = Body::EARTH;
  spec.atmosphere = AtmosphereFidelity::EMPIRICAL;
  EnvironmentModels env = makeEnvironment(spec);
  EXPECT_NE(env.gravity, nullptr);
  EXPECT_NE(env.terrain, nullptr);
  EXPECT_EQ(env.atmosphere, nullptr);
}

TEST(EnvironmentFactory, EnvironmentSpecDefaultsAllConstant) {
  // Default-constructed EnvironmentSpec: body=OTHER, all fidelities CONSTANT.
  // Useful as a "minimal viable env" baseline that costs nothing at runtime.
  sim::environment::EnvironmentSpec spec{};
  EnvironmentModels env = makeEnvironment(spec);
  ASSERT_NE(env.gravity, nullptr);
  ASSERT_NE(env.terrain, nullptr);
  ASSERT_NE(env.atmosphere, nullptr);
  // Constant gravity returns g0 magnitude.
  const double R[3] = {7e6, 0.0, 0.0};
  double a[3] = {0.0, 0.0, 0.0};
  EXPECT_TRUE(env.gravity->acceleration(R, a));
  EXPECT_GT(std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]), 0.0);
  // Constant terrain returns 0.
  double H = 999.0;
  EXPECT_TRUE(ter::isSuccess(env.terrain->elevationAt(0.0, 0.0, H)));
  EXPECT_DOUBLE_EQ(H, 0.0);
  // Constant atmosphere defaults to vacuum for OTHER.
  EXPECT_TRUE(env.atmosphere->isVacuum());
}

/* ----------------------------- Pretty-print ----------------------------- */

TEST(EnvironmentFactory, FidelityToStringRoundtrip) {
  EXPECT_STREQ(sim::environment::toString(GravityFidelity::CONSTANT), "CONSTANT");
  EXPECT_STREQ(sim::environment::toString(GravityFidelity::J2), "J2");
  EXPECT_STREQ(sim::environment::toString(GravityFidelity::SPHERICAL), "SPHERICAL");
  EXPECT_STREQ(sim::environment::toString(TerrainFidelity::CONSTANT), "CONSTANT");
  EXPECT_STREQ(sim::environment::toString(TerrainFidelity::SPHERE), "SPHERE");
  EXPECT_STREQ(sim::environment::toString(TerrainFidelity::ELLIPSOID), "ELLIPSOID");
  EXPECT_STREQ(sim::environment::toString(TerrainFidelity::HTILE), "HTILE");
  EXPECT_STREQ(sim::environment::toString(AtmosphereFidelity::CONSTANT), "CONSTANT");
  EXPECT_STREQ(sim::environment::toString(AtmosphereFidelity::EXPONENTIAL), "EXPONENTIAL");
  EXPECT_STREQ(sim::environment::toString(AtmosphereFidelity::LAYERED), "LAYERED");
  EXPECT_STREQ(sim::environment::toString(AtmosphereFidelity::EMPIRICAL), "EMPIRICAL");
  EXPECT_STREQ(sim::environment::toString(Body::EARTH), "EARTH");
  EXPECT_STREQ(sim::environment::toString(Body::MOON), "MOON");
  EXPECT_STREQ(sim::environment::toString(Body::OTHER), "OTHER");
}

TEST(EnvironmentFactory, ToStringHandlesOutOfRangeValues) {
  // The default arm of each pretty-printer returns a stable UNKNOWN_* string
  // for a value outside the enum, so logging a corrupted value never reads
  // out of bounds.
  EXPECT_STREQ(sim::environment::toString(static_cast<Body>(200)), "UNKNOWN_BODY");
  EXPECT_STREQ(sim::environment::toString(static_cast<GravityFidelity>(200)),
               "UNKNOWN_GRAVITY_FIDELITY");
  EXPECT_STREQ(sim::environment::toString(static_cast<TerrainFidelity>(200)),
               "UNKNOWN_TERRAIN_FIDELITY");
  EXPECT_STREQ(sim::environment::toString(static_cast<AtmosphereFidelity>(200)),
               "UNKNOWN_ATMOSPHERE_FIDELITY");
}

} // namespace
