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
#include "src/sim/environment/atmosphere/inc/Atm.hpp"
#include "src/sim/environment/atmosphere/inc/LayeredAtmosphere.hpp"
#include "src/sim/environment/world/inc/WorldBundle.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
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

/* ----------------------------- World binding contract ----------------------------- */

namespace worldfx {

/// Write a minimal valid USSA76-class .atm image and pack it as the
/// sole atmosphere entry of an earth world bundle in `dir`.
/// Returns the bundle's content hash (the value a pin must match).
inline std::uint64_t makeEarthWorld(const std::filesystem::path& dir, std::uint32_t uid,
                                    const char* bundleName = "earth.world.tprm",
                                    std::uint16_t nRecords = 2) {
  namespace atm = sim::environment::atmosphere;
  namespace wb = sim::environment::world;
  atm::AtmHeader hdr{};
  atm::atmHeaderInit(hdr);
  std::snprintf(hdr.body, sizeof(hdr.body), "%s", "earth");
  hdr.model_type = static_cast<std::uint8_t>(atm::AtmModelType::kLayered);
  hdr.n_records = nRecords;
  const std::filesystem::path SRC = dir / (std::string(bundleName) + ".fixture.atm");
  atm::AtmWriter w;
  if (!w.open(SRC.string().c_str(), hdr)) {
    return 0;
  }
  std::vector<atm::AtmRecord> recs;
  recs.reserve(nRecords);
  for (std::uint16_t i = 0; i < nRecords; ++i) {
    recs.push_back(atm::atmMakeLayer(11000.0 * i, 288.15 - 71.5 * (i > 0 ? 1.0 : 0.0),
                                     i == 0 ? 101325.0 : 22632.06 / i, 0.0));
  }
  if (!w.writeAllRecords(recs.data(), recs.size())) {
    return 0;
  }
  w.close();
  const std::filesystem::path OUT = dir / bundleName;
  if (wb::WorldBundleWriter::write(OUT, uid, "earth",
                                   {{wb::WorldEntryRole::ATMOSPHERE, ".atm", SRC, 0}}) !=
      wb::WorldBundleCheck::OK) {
    return 0;
  }
  wb::WorldBundleReader r;
  if (r.open(OUT) != wb::WorldBundleCheck::OK) {
    return 0;
  }
  return r.header().bundleContentHash;
}

/// Unique temp dir per test.
inline std::filesystem::path freshDir(const char* hint) {
  static int counter = 0;
  const auto D = std::filesystem::temp_directory_path() /
                 (std::string("cb_world_") + hint + "_" + std::to_string(::getpid()) + "_" +
                  std::to_string(counter++));
  std::filesystem::create_directories(D);
  return D;
}

} // namespace worldfx

// A file-backed fidelity with no world binding fails tunables
// validation: the config must name the world it needs.
TEST(CelestialBodyWorld, FileBackedFidelityWithoutBindingRefused) {
  CelestialBodyTunables t = analyticEarth();
  t.atmosphere_fidelity = AtmosphereFidelity::LAYERED;
  t.world_uid = 0;
  CelestialBody body;
  body.tunables().set(t);
  EXPECT_NE(body.init(), 0u);
  EXPECT_FALSE(body.isReady());
}

// A binding that resolves to no bundle (empty bank dir) refuses init.
TEST(CelestialBodyWorld, MissingBundleRefused) {
  const auto DIR = worldfx::freshDir("missing");
  CelestialBodyTunables t = analyticEarth();
  t.atmosphere_fidelity = AtmosphereFidelity::LAYERED;
  t.world_uid = sim::environment::world::worldFullUid(0x0101);
  t.world_pin = 0x1234;
  CelestialBody body;
  ASSERT_EQ(body.loadTprm(DIR), system_core::system_component::TprmIngest::DEFAULTS);
  body.tunables().set(t);
  EXPECT_NE(body.init(), 0u);
  EXPECT_EQ(body.bodyState().init_status, 2u);
  std::filesystem::remove_all(DIR);
}

// The happy path: bundle present, uid matches, pin matches -> the
// atmosphere loads from the entry payload and the state block carries
// the bound world identity.
TEST(CelestialBodyWorld, BoundWorldLoadsAtmosphere) {
  const auto DIR = worldfx::freshDir("bound");
  const std::uint32_t UID = sim::environment::world::worldFullUid(0x0101);
  const std::uint64_t PIN = worldfx::makeEarthWorld(DIR, UID);
  ASSERT_NE(PIN, 0u);

  CelestialBodyTunables t = analyticEarth();
  t.atmosphere_fidelity = AtmosphereFidelity::LAYERED;
  t.world_uid = UID;
  t.world_pin = PIN;
  CelestialBody body;
  ASSERT_EQ(body.loadTprm(DIR), system_core::system_component::TprmIngest::DEFAULTS);
  body.tunables().set(t);
  ASSERT_EQ(body.init(), 0u);
  EXPECT_TRUE(body.isReady());
  EXPECT_EQ(body.bodyState().data_loaded, 1u);
  EXPECT_EQ(body.bodyState().world_uid, UID);
  EXPECT_EQ(body.bodyState().world_pin, PIN);
  // The loaded table answers: sea-level density in the USSA76 ballpark.
  ASSERT_NE(body.atmosphere(), nullptr);
  sim::environment::atmosphere::AtmosphereState state{};
  ASSERT_EQ(body.atmosphere()->query(0.0, 0.0, 0.0, state),
            sim::environment::atmosphere::Status::SUCCESS);
  EXPECT_NEAR(state.rho, 1.225, 0.01);
  std::filesystem::remove_all(DIR);
}

// A pin mismatch refuses the world outright -- the master authorizes
// exactly one bundle content, and this is not it.
TEST(CelestialBodyWorld, PinMismatchRefused) {
  const auto DIR = worldfx::freshDir("pin");
  const std::uint32_t UID = sim::environment::world::worldFullUid(0x0101);
  const std::uint64_t PIN = worldfx::makeEarthWorld(DIR, UID);
  ASSERT_NE(PIN, 0u);

  CelestialBodyTunables t = analyticEarth();
  t.atmosphere_fidelity = AtmosphereFidelity::LAYERED;
  t.world_uid = UID;
  t.world_pin = PIN ^ 0x1ull; // one bit off the authorized content
  CelestialBody body;
  ASSERT_EQ(body.loadTprm(DIR), system_core::system_component::TprmIngest::DEFAULTS);
  body.tunables().set(t);
  EXPECT_NE(body.init(), 0u);
  EXPECT_EQ(body.bodyState().init_status, 2u);
  EXPECT_EQ(body.bodyState().data_loaded, 0u);
  std::filesystem::remove_all(DIR);
}

// A fidelity demanding a role the bound world lacks refuses init:
// terrain HTILE against an atmosphere-only bundle.
TEST(CelestialBodyWorld, MissingRoleRefused) {
  const auto DIR = worldfx::freshDir("role");
  const std::uint32_t UID = sim::environment::world::worldFullUid(0x0101);
  const std::uint64_t PIN = worldfx::makeEarthWorld(DIR, UID);
  ASSERT_NE(PIN, 0u);

  CelestialBodyTunables t = analyticEarth();
  t.terrain_fidelity = TerrainFidelity::HTILE;
  t.world_uid = UID;
  t.world_pin = PIN;
  CelestialBody body;
  ASSERT_EQ(body.loadTprm(DIR), system_core::system_component::TprmIngest::DEFAULTS);
  body.tunables().set(t);
  EXPECT_NE(body.init(), 0u);
  EXPECT_EQ(body.bodyState().init_status, 2u);
  std::filesystem::remove_all(DIR);
}

// Two versions of one world coexist in the bank; the pin selects the
// authorized one regardless of scan order.
TEST(CelestialBodyWorld, TwoVersionsPinSelects) {
  const auto DIR = worldfx::freshDir("twover");
  const std::uint32_t UID = sim::environment::world::worldFullUid(0x0101);
  const std::uint64_t PIN_V1 = worldfx::makeEarthWorld(DIR, UID, "earth.world.tprm", 2);
  const std::uint64_t PIN_V2 = worldfx::makeEarthWorld(DIR, UID, "earth_v2.world.tprm", 3);
  ASSERT_NE(PIN_V1, 0u);
  ASSERT_NE(PIN_V2, 0u);
  ASSERT_NE(PIN_V1, PIN_V2);

  CelestialBodyTunables t = analyticEarth();
  t.atmosphere_fidelity = AtmosphereFidelity::LAYERED;
  t.world_uid = UID;
  t.world_pin = PIN_V2;
  CelestialBody body;
  ASSERT_EQ(body.loadTprm(DIR), system_core::system_component::TprmIngest::DEFAULTS);
  body.tunables().set(t);
  ASSERT_EQ(body.init(), 0u);
  EXPECT_EQ(body.bodyState().world_pin, PIN_V2);
  const auto* atm =
      dynamic_cast<const sim::environment::atmosphere::LayeredAtmosphere*>(body.atmosphere());
  ASSERT_NE(atm, nullptr);
  EXPECT_EQ(atm->fileHeader().n_records, 3u); // v2's table, not v1's
  std::filesystem::remove_all(DIR);
}

// The rebind re-entry: after init, a second bindWorld pass under new
// tunables (the RELOAD_TPRM shape) swaps the running world in place;
// a pin-revert swaps back -- both versions stayed resident.
TEST(CelestialBodyWorld, RebindSwapsAndRevertsInPlace) {
  const auto DIR = worldfx::freshDir("rebind");
  const std::uint32_t UID = sim::environment::world::worldFullUid(0x0101);
  const std::uint64_t PIN_V1 = worldfx::makeEarthWorld(DIR, UID, "earth.world.tprm", 2);
  const std::uint64_t PIN_V2 = worldfx::makeEarthWorld(DIR, UID, "earth_v2.world.tprm", 3);
  ASSERT_NE(PIN_V1, 0u);
  ASSERT_NE(PIN_V2, 0u);

  CelestialBodyTunables t = analyticEarth();
  t.atmosphere_fidelity = AtmosphereFidelity::LAYERED;
  t.world_uid = UID;
  t.world_pin = PIN_V1;
  CelestialBody body;
  ASSERT_EQ(body.loadTprm(DIR), system_core::system_component::TprmIngest::DEFAULTS);
  body.tunables().set(t);
  ASSERT_EQ(body.init(), 0u);
  const auto* atm =
      dynamic_cast<const sim::environment::atmosphere::LayeredAtmosphere*>(body.atmosphere());
  ASSERT_NE(atm, nullptr);
  EXPECT_EQ(atm->fileHeader().n_records, 2u);

  // Rebind to v2 (tunables update + re-entry), model swaps in place.
  t.world_pin = PIN_V2;
  body.tunables().set(t);
  ASSERT_EQ(body.loadTprm(DIR), system_core::system_component::TprmIngest::DEFAULTS);
  EXPECT_EQ(body.bodyState().world_pin, PIN_V2);
  EXPECT_EQ(body.atmosphere(), atm); // same model object, new content
  EXPECT_EQ(atm->fileHeader().n_records, 3u);

  // Revert to v1: the prior version never left the bank.
  t.world_pin = PIN_V1;
  body.tunables().set(t);
  ASSERT_EQ(body.loadTprm(DIR), system_core::system_component::TprmIngest::DEFAULTS);
  EXPECT_EQ(body.bodyState().world_pin, PIN_V1);
  EXPECT_EQ(atm->fileHeader().n_records, 2u);
  std::filesystem::remove_all(DIR);
}

} // namespace
