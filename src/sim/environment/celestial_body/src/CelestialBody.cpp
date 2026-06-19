/**
 * @file CelestialBody.cpp
 * @brief Implementation of the CelestialBody apex component.
 */

#include "src/sim/environment/celestial_body/inc/CelestialBody.hpp"

#include "src/sim/environment/atmosphere/inc/AtmosphereModelBase.hpp"
#include "src/sim/environment/atmosphere/inc/AtmosphereStatus.hpp"
#include "src/sim/environment/atmosphere/inc/LayeredAtmosphere.hpp"
#include "src/sim/environment/factory/inc/EnvironmentFactory.hpp"
#include "src/sim/environment/gravity/inc/earth/Wgs84Constants.hpp"
#include "src/sim/environment/gravity/inc/moon/LunarConstants.hpp"
#include "src/sim/environment/terrain/inc/HtileTile.hpp"
#include "src/sim/environment/terrain/inc/TerrainStatus.hpp"
#include "src/utilities/helpers/inc/Files.hpp"

#include <cmath>
#include <cstring>
#include <fmt/format.h>
#include <optional>
#include <string>
#include <system_error>

namespace sim {
namespace environment {
namespace celestial_body {

namespace env = sim::environment;

/* ----------------------------- File Helpers ----------------------------- */

namespace {

/// Returns true iff the path buffer holds at least one non-NUL char.
bool pathSet(const char (&buf)[MAX_DATA_PATH]) noexcept { return buf[0] != '\0'; }

/// Returns the canonical reference radius for a built-in body, or 0 if the
/// body is OTHER. A procedural (OTHER) body has no canonical radius here, so
/// surface-gravity telemetry is left at 0 for it (see doInit()).
double referenceRadiusFor(env::Body body) noexcept {
  switch (body) {
  case env::Body::EARTH:
    return env::gravity::wgs84::A;
  case env::Body::MOON:
    return env::gravity::lunar::R_REF;
  case env::Body::OTHER:
    return 0.0;
  }
  return 0.0;
}

/// Validates the tunables struct for internal consistency.
/// Currently: file-backed fidelities require non-empty paths.
bool tunablesOk(const CelestialBodyTunables& t) noexcept {
  if (t.gravity_fidelity == env::GravityFidelity::SPHERICAL && !pathSet(t.gravity_data_path)) {
    return false;
  }
  if (t.terrain_fidelity == env::TerrainFidelity::HTILE && !pathSet(t.terrain_data_path)) {
    return false;
  }
  if (t.atmosphere_fidelity == env::AtmosphereFidelity::LAYERED &&
      !pathSet(t.atmosphere_data_path)) {
    return false;
  }
  return true;
}

} // namespace

/* ----------------------------- CelestialBody Methods ----------------------------- */

bool CelestialBody::loadTprm(const std::filesystem::path& tprmDir) noexcept {
  // The apex executive's unpackMasterTprm() writes each entry to disk
  // as `tprmDir/{fullUid:06x}.tprm`. Look for one for this instance;
  // if absent, the C++ struct defaults stand and we report success
  // (loadTprm is optional per the framework contract).
  const std::filesystem::path PATH = tprmDir / fmt::format("{:06x}.tprm", fullUid());
  std::error_code ec;
  if (!std::filesystem::exists(PATH, ec)) {
    return true;
  }
  std::string err;
  std::optional<std::reference_wrapper<std::string>> errRef{err};
  if (!apex::helpers::files::hex2cpp(PATH.string(), tunables_.get(), errRef)) {
    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(), fmt::format("loadTprm: hex2cpp failed for {} ({})", PATH.string(), err));
    }
    return false;
  }
  auto* log = componentLog();
  if (log != nullptr) {
    log->info(label(), fmt::format("loadTprm: tunables loaded from {}", PATH.string()));
  }
  return true;
}

std::uint8_t CelestialBody::doInit() noexcept {
  using system_core::data::DataCategory;

  auto& s = state_.get();
  s.env_built = 0;
  s.data_loaded = 0;
  s.init_status = 0;

  const auto& p = tunables_.get();

  // 1. Validate tunables.
  if (!tunablesOk(p)) {
    s.init_status = 2;
    auto* log = componentLog();
    if (log != nullptr) {
      log->info(
          label(),
          fmt::format("init: tunables invalid (file-backed fidelity selected with empty path)"));
    }
    return static_cast<std::uint8_t>(ApexStatus::ERROR_PARAM);
  }

  // 2. Build env via the factory.
  env::EnvironmentSpec spec{};
  spec.body = p.body;
  spec.gravity = p.gravity_fidelity;
  spec.terrain = p.terrain_fidelity;
  spec.atmosphere = p.atmosphere_fidelity;
  env_ = env::makeEnvironment(spec);

  if (env_.gravity == nullptr || env_.terrain == nullptr || env_.atmosphere == nullptr) {
    s.init_status = 2;
    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(), "init: factory returned a null model");
    }
    return static_cast<std::uint8_t>(ApexStatus::ERROR_LOAD_INVALID);
  }
  s.env_built = 1;

  // 3. Load file-backed models when their fidelity needs files. The terrain
  //    and atmosphere load() calls now return their own Status enums (a
  //    successful load is Status::SUCCESS); a dynamic_cast miss or any
  //    non-success load is a fatal init error (the specific code is logged).
  bool ok = true;
  if (p.terrain_fidelity == env::TerrainFidelity::HTILE) {
    auto* tile = dynamic_cast<env::terrain::HtileTile*>(env_.terrain.get());
    if (tile == nullptr) {
      auto* log = componentLog();
      if (log != nullptr) {
        log->info(label(), "init: terrain model is not an HtileTile");
      }
      ok = false;
    } else {
      const env::terrain::Status tstatus = tile->load(std::string(p.terrain_data_path));
      if (!env::terrain::isSuccess(tstatus)) {
        auto* log = componentLog();
        if (log != nullptr) {
          log->info(label(), fmt::format("init: terrain load failed ({}) -> {}",
                                         env::terrain::toString(tstatus), p.terrain_data_path));
        }
        ok = false;
      }
    }
  }
  if (p.atmosphere_fidelity == env::AtmosphereFidelity::LAYERED) {
    auto* atm = dynamic_cast<env::atmosphere::LayeredAtmosphere*>(env_.atmosphere.get());
    if (atm == nullptr) {
      auto* log = componentLog();
      if (log != nullptr) {
        log->info(label(), "init: atmosphere model is not a LayeredAtmosphere");
      }
      ok = false;
    } else {
      const env::atmosphere::Status astatus = atm->load(std::string(p.atmosphere_data_path));
      if (!env::atmosphere::isSuccess(astatus)) {
        auto* log = componentLog();
        if (log != nullptr) {
          log->info(label(),
                    fmt::format("init: atmosphere load failed ({}) -> {}",
                                env::atmosphere::toString(astatus), p.atmosphere_data_path));
        }
        ok = false;
      }
    }
  }
  // Gravity SPHERICAL fidelity: the gravity model's coefficient-loading API
  // (a CoeffSource the caller wires + the model's own init()) differs from the
  // terrain/atmosphere load() contract, so it is not file-loaded here; the
  // analytic/J2 arms need no file at all. The validation step already requires
  // a non-empty gravity_data_path for SPHERICAL.

  if (!ok) {
    s.init_status = 2;
    return static_cast<std::uint8_t>(ApexStatus::ERROR_LOAD_INVALID);
  }
  s.data_loaded = 1;

  // 4. Populate OUTPUT telemetry: a one-shot snapshot of body identity
  //    + key physical summary derived from the live env models. This
  //    is the public face other components subscribe to and that an
  //    external bridge reads to forward to UE5 / ground systems.
  auto& tlm = telemetry_.get();
  tlm.body = static_cast<std::uint8_t>(p.body);
  tlm.gravity_fidelity = static_cast<std::uint8_t>(p.gravity_fidelity);
  tlm.terrain_fidelity = static_cast<std::uint8_t>(p.terrain_fidelity);
  tlm.atmosphere_fidelity = static_cast<std::uint8_t>(p.atmosphere_fidelity);
  tlm.is_vacuum_atmosphere = env_.atmosphere->isVacuum() ? 1u : 0u;
  tlm.reference_radius_m = referenceRadiusFor(p.body);
  tlm.gravity_max_degree = env_.gravity->maxDegree();

  // Surface gravity: query the polymorphic model at radius=ref_radius
  // along +X. Skip if reference radius unknown (Body::OTHER).
  tlm.surface_gravity_m_s2 = 0.0;
  if (tlm.reference_radius_m > 0.0) {
    const double R[3] = {tlm.reference_radius_m, 0.0, 0.0};
    double a[3] = {0.0, 0.0, 0.0};
    if (env_.gravity->acceleration(R, a)) {
      tlm.surface_gravity_m_s2 = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    }
  }

  // Surface atmosphere snapshot at altitude=0 (sea level / surface). query()
  // takes (alt_m, lat_rad, lon_rad), so this samples the surface on the
  // equator. The atmosphere models now return env::atmosphere::Status rather
  // than a bool: only a SUCCESS sample populates telemetry. A vacuum model is
  // already short-circuited above; a non-success result here (e.g. an
  // uninitialized model) leaves the snapshot at 0 and is logged.
  tlm.surface_atmosphere_density_kg_m3 = 0.0;
  tlm.surface_atmosphere_temperature_K = 0.0;
  if (!tlm.is_vacuum_atmosphere) {
    env::atmosphere::AtmosphereState astate{};
    const env::atmosphere::Status astatus = env_.atmosphere->query(0.0, 0.0, 0.0, astate);
    if (env::atmosphere::isSuccess(astatus)) {
      tlm.surface_atmosphere_density_kg_m3 = astate.rho;
      tlm.surface_atmosphere_temperature_K = astate.T;
    } else {
      auto* log = componentLog();
      if (log != nullptr) {
        log->info(label(), fmt::format("init: surface atmosphere query non-success ({})",
                                       env::atmosphere::toString(astatus)));
      }
    }
  }

  // 5. Expose tunables + state + telemetry through the data registry.
  //    Other components can find a CelestialBody by its fullUid + category.
  registerData(DataCategory::TUNABLE_PARAM, "tunables", &tunables_.get(),
               sizeof(CelestialBodyTunables));
  registerData(DataCategory::STATE, "state", &state_.get(), sizeof(CelestialBodyState));
  registerData(DataCategory::OUTPUT, "telemetry", &telemetry_.get(),
               sizeof(CelestialBodyTelemetry));

  // 6. (Passive component: no registerTask calls.)

  s.init_status = 1;
  tlm.init_status = 1;

  auto* log = componentLog();
  if (log != nullptr) {
    log->info(label(), fmt::format("init: body={} g={} t={} a={} env_built={} data_loaded={}",
                                   static_cast<int>(p.body), static_cast<int>(p.gravity_fidelity),
                                   static_cast<int>(p.terrain_fidelity),
                                   static_cast<int>(p.atmosphere_fidelity),
                                   static_cast<int>(s.env_built), static_cast<int>(s.data_loaded)));
  }

  return static_cast<std::uint8_t>(ApexStatus::SUCCESS);
}

} // namespace celestial_body
} // namespace environment
} // namespace sim
