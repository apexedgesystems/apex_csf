/**
 * @file CelestialBody_uTest.cpp
 * @brief Tests for the CelestialBody apex component.
 *
 * Coverage:
 *   - Default-constructed component is not ready.
 *   - Tunables can be set via tunables().set(...).
 *   - doInit() succeeds with analytic-only fidelities (Earth, Moon, OTHER).
 *   - doInit() rejects every file-backed fidelity with an empty path
 *     (gravity SPHERICAL, terrain HTILE, atmosphere LAYERED).
 *   - File-backed load failures (invalid path) propagate to a failed init.
 *   - Telemetry reflects the body summary, including the vacuum and
 *     no-canonical-radius (OTHER) cases, and CONSTANT-gravity max degree.
 *   - After successful init, gravity()/terrain()/atmosphere() return
 *     non-null and queries route through them correctly (gravity is a bool
 *     API; terrain/atmosphere return env Status).
 *   - bodyState() reflects lifecycle progress; init() is idempotent.
 *
 * Note: doInit() is normally invoked by the executive's
 * registerComponent(), which goes through the apex SystemComponentBase
 * lifecycle. For unit-test scope we drive doInit() through the public
 * init() shim that the base class provides.
 */

#include "src/sim/environment/celestial_body/inc/CelestialBody.hpp"
#include "src/sim/environment/celestial_body/inc/CelestialBodyData.hpp"
#include "src/sim/environment/atmosphere/inc/AtmosphereModelBase.hpp"
#include "src/sim/environment/atmosphere/inc/AtmosphereStatus.hpp"
#include "src/sim/environment/atmosphere/inc/ConstantAtmosphere.hpp"
#include "src/sim/environment/factory/inc/Body.hpp"
#include "src/sim/environment/factory/inc/EnvironmentFidelity.hpp"
#include "src/sim/environment/terrain/inc/TerrainStatus.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstring>

using sim::environment::AtmosphereFidelity;
using sim::environment::Body;
using sim::environment::GravityFidelity;
using sim::environment::TerrainFidelity;
using sim::environment::celestial_body::CelestialBody;
using sim::environment::celestial_body::CelestialBodyTunables;

namespace {

/// Build a tunables struct selecting purely analytic fidelities (no data files).
CelestialBodyTunables analyticEarth() {
  CelestialBodyTunables t{};
  t.body = Body::EARTH;
  t.gravity_fidelity = GravityFidelity::J2;
  t.terrain_fidelity = TerrainFidelity::ELLIPSOID;
  t.atmosphere_fidelity = AtmosphereFidelity::EXPONENTIAL;
  return t;
}

CelestialBodyTunables analyticMoon() {
  CelestialBodyTunables t{};
  t.body = Body::MOON;
  t.gravity_fidelity = GravityFidelity::J2;
  t.terrain_fidelity = TerrainFidelity::SPHERE;
  t.atmosphere_fidelity = AtmosphereFidelity::CONSTANT; // vacuum
  return t;
}

/* ----------------------------- Default Construction ----------------------------- */

TEST(CelestialBody, DefaultIsNotReady) {
  CelestialBody body;
  EXPECT_FALSE(body.isReady());
  EXPECT_EQ(body.bodyState().init_status, 0u);
  EXPECT_EQ(body.gravity(), nullptr);
  EXPECT_EQ(body.terrain(), nullptr);
  EXPECT_EQ(body.atmosphere(), nullptr);
}

TEST(CelestialBody, ComponentIdentityMatchesContract) {
  CelestialBody body;
  EXPECT_EQ(body.componentId(), 220u);
  EXPECT_STREQ(body.componentName(), "CelestialBody");
  EXPECT_STREQ(body.label(), "CELESTIAL_BODY");
}

/* ----------------------------- Tunables Set + Init (analytic) ----------------------------- */

TEST(CelestialBody, EarthAnalyticInitSucceeds) {
  CelestialBody earth;
  earth.tunables().set(analyticEarth());
  // Drive doInit() via the public init() lifecycle.
  ASSERT_EQ(earth.init(), 0u) << "init() should return SUCCESS for analytic Earth";
  EXPECT_TRUE(earth.isReady());
  EXPECT_EQ(earth.bodyState().init_status, 1u);
  EXPECT_EQ(earth.bodyState().env_built, 1u);
  EXPECT_EQ(earth.bodyState().data_loaded, 1u);
  ASSERT_NE(earth.gravity(), nullptr);
  ASSERT_NE(earth.terrain(), nullptr);
  ASSERT_NE(earth.atmosphere(), nullptr);
}

TEST(CelestialBody, MoonAnalyticInitSucceeds) {
  CelestialBody moon;
  moon.tunables().set(analyticMoon());
  ASSERT_EQ(moon.init(), 0u);
  EXPECT_TRUE(moon.isReady());
  // Moon CONSTANT atmosphere defaults to vacuum.
  ASSERT_NE(moon.atmosphere(), nullptr);
  EXPECT_TRUE(moon.atmosphere()->isVacuum());
}

/* ----------------------------- Telemetry (OUTPUT) ----------------------------- */

TEST(CelestialBody, EarthTelemetryReflectsBodySummary) {
  CelestialBody earth;
  earth.tunables().set(analyticEarth());
  ASSERT_EQ(earth.init(), 0u);
  const auto& tlm = earth.telemetry();
  EXPECT_EQ(tlm.body, static_cast<std::uint8_t>(Body::EARTH));
  EXPECT_EQ(tlm.gravity_fidelity, static_cast<std::uint8_t>(GravityFidelity::J2));
  EXPECT_EQ(tlm.terrain_fidelity, static_cast<std::uint8_t>(TerrainFidelity::ELLIPSOID));
  EXPECT_EQ(tlm.atmosphere_fidelity, static_cast<std::uint8_t>(AtmosphereFidelity::EXPONENTIAL));
  EXPECT_EQ(tlm.init_status, 1u);
  EXPECT_EQ(tlm.is_vacuum_atmosphere, 0u);             // Earth EXPONENTIAL is not vacuum
  EXPECT_NEAR(tlm.reference_radius_m, 6378137.0, 1.0); // WGS84 equatorial
  // Surface gravity at Earth's equatorial radius: J2 returns ~9.8 m/s^2.
  EXPECT_GT(tlm.surface_gravity_m_s2, 9.0);
  EXPECT_LT(tlm.surface_gravity_m_s2, 11.0);
  // ISA sea-level density.
  EXPECT_NEAR(tlm.surface_atmosphere_density_kg_m3, 1.225, 1e-3);
  EXPECT_NEAR(tlm.surface_atmosphere_temperature_K, 288.15, 0.1);
}

TEST(CelestialBody, MoonTelemetryReflectsVacuum) {
  CelestialBody moon;
  moon.tunables().set(analyticMoon());
  ASSERT_EQ(moon.init(), 0u);
  const auto& tlm = moon.telemetry();
  EXPECT_EQ(tlm.body, static_cast<std::uint8_t>(Body::MOON));
  EXPECT_EQ(tlm.is_vacuum_atmosphere, 1u);
  // Lunar reference radius from the GRAVITY constants (1738.0 km).
  // Note: the terrain library uses 1737.4 km; gravity uses 1738.0 km.
  // Telemetry mirrors gravity since reference radius is used to compute
  // surface gravity from the gravity model.
  EXPECT_NEAR(tlm.reference_radius_m, 1738000.0, 1.0);
  // Lunar surface gravity ~ 1.62 m/s^2.
  EXPECT_GT(tlm.surface_gravity_m_s2, 1.5);
  EXPECT_LT(tlm.surface_gravity_m_s2, 1.8);
  EXPECT_DOUBLE_EQ(tlm.surface_atmosphere_density_kg_m3, 0.0);
  EXPECT_DOUBLE_EQ(tlm.surface_atmosphere_temperature_K, 0.0);
}

/* ----------------------------- Validation ----------------------------- */

TEST(CelestialBody, RejectsHtileTerrainWithoutDataPath) {
  CelestialBodyTunables t = analyticEarth();
  t.terrain_fidelity = TerrainFidelity::HTILE;
  // gravity_data_path / terrain_data_path / atmosphere_data_path all empty
  CelestialBody body;
  body.tunables().set(t);
  EXPECT_NE(body.init(), 0u);
  EXPECT_FALSE(body.isReady());
  EXPECT_EQ(body.bodyState().init_status, 2u);
}

TEST(CelestialBody, RejectsLayeredAtmosphereWithoutDataPath) {
  CelestialBodyTunables t = analyticEarth();
  t.atmosphere_fidelity = AtmosphereFidelity::LAYERED;
  CelestialBody body;
  body.tunables().set(t);
  EXPECT_NE(body.init(), 0u);
  EXPECT_FALSE(body.isReady());
}

/* ----------------------------- Functional queries ----------------------------- */

TEST(CelestialBody, GravityQueryReturnsAccelerationForEarth) {
  CelestialBody earth;
  earth.tunables().set(analyticEarth());
  ASSERT_EQ(earth.init(), 0u);
  // Query gravity at a 7000-km radial position; expect ~ -8 m/s^2 inward.
  const double R[3] = {7.0e6, 0.0, 0.0};
  double a[3] = {0.0, 0.0, 0.0};
  ASSERT_TRUE(earth.gravity()->acceleration(R, a));
  const double MAG = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  EXPECT_GT(MAG, 5.0); // sanity bounds; J2 gravity at 7000 km is ~ 8 m/s^2
  EXPECT_LT(MAG, 12.0);
}

TEST(CelestialBody, AtmosphereQueryAtSeaLevelEarth) {
  CelestialBody earth;
  earth.tunables().set(analyticEarth());
  ASSERT_EQ(earth.init(), 0u);
  // Earth EXPONENTIAL atmosphere defaults to ISA tropo: rho0 = 1.225 kg/m^3.
  // density() now returns env::atmosphere::Status; a sea-level sample is
  // SUCCESS.
  double rho = 0.0;
  EXPECT_TRUE(
      sim::environment::atmosphere::isSuccess(earth.atmosphere()->density(0.0, 0.0, 0.0, rho)));
  EXPECT_NEAR(rho, 1.225, 1e-6);
}

TEST(CelestialBody, TerrainQueryAtEcefForMoonSphere) {
  CelestialBody moon;
  moon.tunables().set(analyticMoon());
  ASSERT_EQ(moon.init(), 0u);
  // ECEF point on the lunar surface (mean radius 1737.4 km on +x axis).
  // elevationAtEcef() now returns env::terrain::Status; a sphere datum query
  // on the surface is SUCCESS.
  const double EC[3] = {1737400.0, 0.0, 0.0};
  double H = -1.0;
  EXPECT_TRUE(sim::environment::terrain::isSuccess(moon.terrain()->elevationAtEcef(EC, H)));
  EXPECT_NEAR(H, 0.0, 1e-3);
}

/* ----------------------------- OTHER body / no canonical radius ----------------------------- */

// A procedural (OTHER) body has no canonical reference radius, so surface
// gravity telemetry is left at 0 -- this exercises the referenceRadiusFor
// OTHER arm and the `reference_radius_m > 0.0` false branch in doInit().
TEST(CelestialBody, OtherBodyAnalyticInitLeavesSurfaceGravityZero) {
  CelestialBodyTunables t{};
  t.body = Body::OTHER;
  t.gravity_fidelity = GravityFidelity::J2;
  t.terrain_fidelity = TerrainFidelity::SPHERE;
  t.atmosphere_fidelity = AtmosphereFidelity::EXPONENTIAL;
  CelestialBody body;
  body.tunables().set(t);
  ASSERT_EQ(body.init(), 0u);
  EXPECT_TRUE(body.isReady());
  const auto& tlm = body.telemetry();
  EXPECT_EQ(tlm.body, static_cast<std::uint8_t>(Body::OTHER));
  EXPECT_DOUBLE_EQ(tlm.reference_radius_m, 0.0);
  EXPECT_DOUBLE_EQ(tlm.surface_gravity_m_s2, 0.0);
  // OTHER + EXPONENTIAL is a non-vacuum atmosphere, so the surface snapshot
  // still populates from the sea-level query.
  EXPECT_EQ(tlm.is_vacuum_atmosphere, 0u);
  EXPECT_GT(tlm.surface_atmosphere_density_kg_m3, 0.0);
}

/* ----------------------------- CONSTANT gravity fidelity ----------------------------- */

// CONSTANT gravity reports maxDegree() == 0; telemetry should mirror that and
// still produce a positive surface-gravity magnitude for Earth.
TEST(CelestialBody, ConstantGravityTelemetryMaxDegreeZero) {
  CelestialBodyTunables t = analyticEarth();
  t.gravity_fidelity = GravityFidelity::CONSTANT;
  CelestialBody earth;
  earth.tunables().set(t);
  ASSERT_EQ(earth.init(), 0u);
  const auto& tlm = earth.telemetry();
  EXPECT_EQ(tlm.gravity_fidelity, static_cast<std::uint8_t>(GravityFidelity::CONSTANT));
  EXPECT_EQ(tlm.gravity_max_degree, 0);
  EXPECT_GT(tlm.surface_gravity_m_s2, 0.0);
}

/* ----------------------------- Validation: gravity SPHERICAL path ----------------------------- */

// SPHERICAL gravity is a file-backed fidelity; an empty data path is rejected
// at the tunables-validation step (init_status == 2, never builds env).
TEST(CelestialBody, RejectsSphericalGravityWithoutDataPath) {
  CelestialBodyTunables t = analyticEarth();
  t.gravity_fidelity = GravityFidelity::SPHERICAL;
  CelestialBody body;
  body.tunables().set(t);
  EXPECT_NE(body.init(), 0u);
  EXPECT_FALSE(body.isReady());
  EXPECT_EQ(body.bodyState().init_status, 2u);
  EXPECT_EQ(body.bodyState().env_built, 0u);
  // Failed init must not expose live models.
  EXPECT_EQ(body.gravity(), nullptr);
}

/* ----------------------------- loadTprm: absent file is a no-op success
 * ----------------------------- */

// loadTprm() is optional: with no .tprm file on disk it returns true and the
// struct defaults (or a prior tunables().set) stand. The component is not yet
// registered (no instance index), so fullUid() resolves to the default UID; an
// empty/temp dir simply has no matching file.
TEST(CelestialBody, InitSucceedsWithoutTprmFile) {
  CelestialBody earth;
  earth.tunables().set(analyticEarth());
  // No loadTprm call here (the executive drives it); init() alone must work
  // off the set() tunables. This also pins the "defaults stand" contract.
  ASSERT_EQ(earth.init(), 0u);
  EXPECT_TRUE(earth.isReady());
}

/* ----------------------------- init() idempotency ----------------------------- */

// The framework's init() is idempotent: a second call returns the cached
// status without rebuilding the environment.
TEST(CelestialBody, InitIsIdempotent) {
  CelestialBody earth;
  earth.tunables().set(analyticEarth());
  ASSERT_EQ(earth.init(), 0u);
  const auto* g0 = earth.gravity();
  EXPECT_EQ(earth.init(), 0u);
  EXPECT_EQ(earth.gravity(), g0); // same model instance, not rebuilt
}

/* ----------------------------- File-backed load failure ----------------------------- */

// HTILE terrain with a non-empty but invalid path passes tunables validation
// (path is set) and builds the env, but the terrain load() then fails -> init
// reports failure (init_status == 2, data_loaded == 0). Exercises the
// load-failure branch and the Status-aware error logging.
TEST(CelestialBody, HtileTerrainLoadFailurePropagates) {
  CelestialBodyTunables t = analyticEarth();
  t.terrain_fidelity = TerrainFidelity::HTILE;
  std::snprintf(t.terrain_data_path, sizeof(t.terrain_data_path), "%s",
                "/nonexistent/path/to/tile.htile");
  CelestialBody body;
  body.tunables().set(t);
  EXPECT_NE(body.init(), 0u);
  EXPECT_FALSE(body.isReady());
  EXPECT_EQ(body.bodyState().init_status, 2u);
  EXPECT_EQ(body.bodyState().env_built, 1u);   // factory built the shell
  EXPECT_EQ(body.bodyState().data_loaded, 0u); // but load failed
}

// LAYERED atmosphere with a non-empty invalid path: same failure shape on the
// atmosphere arm.
TEST(CelestialBody, LayeredAtmosphereLoadFailurePropagates) {
  CelestialBodyTunables t = analyticEarth();
  t.atmosphere_fidelity = AtmosphereFidelity::LAYERED;
  std::snprintf(t.atmosphere_data_path, sizeof(t.atmosphere_data_path), "%s",
                "/nonexistent/path/to/atmo.atm");
  CelestialBody body;
  body.tunables().set(t);
  EXPECT_NE(body.init(), 0u);
  EXPECT_FALSE(body.isReady());
  EXPECT_EQ(body.bodyState().init_status, 2u);
}

} // namespace
