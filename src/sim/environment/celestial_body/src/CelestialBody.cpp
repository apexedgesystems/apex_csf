/**
 * @file CelestialBody.cpp
 * @brief Implementation of the CelestialBody apex component.
 */

#include "src/sim/environment/celestial_body/inc/CelestialBody.hpp"

#include "src/sim/environment/atmosphere/inc/AtmosphereModelBase.hpp"
#include "src/sim/environment/atmosphere/inc/AtmosphereStatus.hpp"
#include "src/sim/environment/atmosphere/inc/LayeredAtmosphere.hpp"
#include "src/sim/environment/factory/inc/EnvironmentFactory.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/TprmPayload.hpp"
#include "src/sim/environment/gravity/inc/earth/Wgs84Constants.hpp"
#include "src/sim/environment/world/inc/WorldBundle.hpp"
#include "src/sim/environment/gravity/inc/moon/LunarConstants.hpp"
#include "src/sim/environment/terrain/inc/HtileTile.hpp"
#include "src/sim/environment/terrain/inc/TerrainStatus.hpp"
#include "src/utilities/helpers/inc/Files.hpp"

#include <cmath>
#include <cstring>
#include <fmt/format.h>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace sim {
namespace environment {
namespace celestial_body {

namespace env = sim::environment;

/* ----------------------------- File Helpers ----------------------------- */

namespace {

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

/// True when any configured fidelity needs world-bundle content.
bool needsWorld(const CelestialBodyTunables& t) noexcept {
  return t.gravity_fidelity == env::GravityFidelity::SPHERICAL ||
         t.terrain_fidelity == env::TerrainFidelity::HTILE ||
         t.atmosphere_fidelity == env::AtmosphereFidelity::LAYERED;
}

/// Validates the tunables struct for internal consistency: file-backed
/// fidelities require a world binding in the reserved uid range.
bool tunablesOk(const CelestialBodyTunables& t) noexcept {
  if (!needsWorld(t)) {
    return true;
  }
  return t.world_uid != 0 && sim::environment::world::isWorldUid(t.world_uid);
}

} // namespace

/* ----------------------------- CelestialBody Methods ----------------------------- */

system_core::system_component::TprmIngest
CelestialBody::loadTprm(const std::filesystem::path& tprmDir) noexcept {
  using system_core::system_component::TprmIngest;
  // The apex executive's unpackMasterTprm() writes each entry to disk
  // as `tprmDir/{fullUid:06x}.tprm`. Look for one for this instance;
  // if absent, the C++ struct defaults stand and we report success
  // (loadTprm is optional per the framework contract).
  if (bootTprmDir_.empty()) {
    bootTprmDir_ = tprmDir;
  }
  lastTprmDir_ = tprmDir;
  const std::filesystem::path PATH = tprmDir / fmt::format("{:06x}.tprm", fullUid());
  std::error_code ec;
  auto* log = componentLog();
  TprmIngest outcome = TprmIngest::DEFAULTS;
  if (std::filesystem::exists(PATH, ec)) {
    namespace sc = system_core::system_component;
    const auto CHECK = sc::readTprmPayload(PATH, fullUid(), tunables_.get());
    if (CHECK != sc::TprmPayloadCheck::OK) {
      if (log != nullptr) {
        log->error(label(), sc::toFaultCode(CHECK),
                   fmt::format("TPRM rejected ({}): {}", sc::toString(CHECK), PATH.string()));
      }
      return TprmIngest::REJECTED;
    }
    if (log != nullptr) {
      log->info(label(), fmt::format("loadTprm: tunables loaded from {}", PATH.string()));
    }
    outcome = TprmIngest::LOADED;
  }

  // Live rebind: a reload landing on an initialized component re-runs
  // the world binding under the new tunables -- RELOAD_TPRM IS the
  // rebind command, no dedicated opcode. Bundle versions coexist in
  // the bank and the pin selects, so the prior world stays resident
  // as the fallback a pin-revert reload restores. Models reload in
  // place (consumer pointers stay valid); the operation belongs under
  // the operator's pause/quiesce discipline. On a failed rebind the
  // running world is kept and the state block keeps attesting it --
  // commanded (tunables) vs actual (state) stay independently
  // INSPECTable, and the refusal is logged with its cause.
  auto& s = state_.get();
  if (s.init_status == 1u) {
    const CelestialBodyTunables& P = tunables_.get();
    if (bindWorld(P, s)) {
      if (log != nullptr) {
        log->info(label(), fmt::format("world rebound: uid=0x{:06x} pin=0x{:016x}", P.world_uid,
                                       P.world_pin));
      }
    } else if (log != nullptr) {
      log->error(label(), static_cast<std::uint8_t>(2),
                 "world rebind refused; prior world remains bound");
    }
  }
  return outcome;
}

bool CelestialBody::bindWorld(const CelestialBodyTunables& p, CelestialBodyState& s) noexcept {
  bool ok = true;
  namespace wb = sim::environment::world;
  wb::WorldBundleReader bundle;
  if (needsWorld(p)) {
    // Discovery matches uid AND pin: bundle versions of one world
    // coexist in the bank (earth.world.tprm beside earth_v2...), and
    // the pin selects the authorized one. A rebind is therefore a
    // tprm reload carrying a new pin; the prior version stays
    // resident as the instant fallback.
    bool bound = false;
    std::uint64_t nearMissHash = 0;
    std::string nearMissName;
    for (const auto& DIR : {lastTprmDir_, bootTprmDir_}) {
      if (bound || DIR.empty()) {
        continue;
      }
      std::error_code ec;
      for (const auto& de : std::filesystem::directory_iterator(DIR, ec)) {
        if (!de.is_regular_file(ec) ||
            !de.path().filename().string().ends_with(wb::WORLD_FILE_SUFFIX)) {
          continue;
        }
        if (bundle.open(de.path()) == wb::WorldBundleCheck::OK &&
            bundle.header().fullUid == p.world_uid) {
          if (bundle.header().bundleContentHash == p.world_pin) {
            bound = true;
            break;
          }
          nearMissHash = bundle.header().bundleContentHash;
          nearMissName = de.path().filename().string();
        }
        bundle.close();
      }
    }
    if (!bound) {
      if (auto* log = componentLog(); log != nullptr) {
        if (!nearMissName.empty()) {
          log->info(label(),
                    fmt::format("init: world pin mismatch: tprm pins 0x{:016x}; nearest uid "
                                "0x{:06x} candidate {} carries 0x{:016x} -- refusing the "
                                "unauthorized world",
                                p.world_pin, p.world_uid, nearMissName, nearMissHash));
        } else {
          log->info(label(),
                    fmt::format("init: no world bundle with uid 0x{:06x} in {} or {}", p.world_uid,
                                lastTprmDir_.string(), bootTprmDir_.string()));
        }
      }
      ok = false;
    } else if (auto* log = componentLog(); log != nullptr) {
      const auto& H = bundle.header();
      log->info(label(),
                fmt::format("world bound: body={} uid=0x{:06x} pin=0x{:016x} entries={}",
                            std::string(H.body, strnlen(H.body, sizeof(H.body))), H.fullUid,
                            H.bundleContentHash, static_cast<unsigned>(H.entryCount)));
    }
  }

  /// Fetch one role's payload; a fidelity that demands a role the
  /// bundle lacks (or a payload failing its CRC) is a fatal miss.
  const auto FETCH = [this, &bundle](wb::WorldEntryRole role, const char* kind,
                                     std::vector<std::uint8_t>& out,
                                     std::uint64_t& specHash) -> bool {
    std::size_t idx = 0;
    if (bundle.findEntry(role, idx) != wb::WorldBundleCheck::OK) {
      if (auto* log = componentLog(); log != nullptr) {
        log->info(label(), fmt::format("init: fidelity requires a {} entry the bound world "
                                       "does not carry",
                                       kind));
      }
      return false;
    }
    const wb::WorldBundleCheck RC = bundle.readEntry(idx, out);
    if (RC != wb::WorldBundleCheck::OK) {
      if (auto* log = componentLog(); log != nullptr) {
        log->info(label(), fmt::format("init: {} entry read failed ({})", kind, wb::toString(RC)));
      }
      return false;
    }
    specHash = bundle.entries()[idx].specHash;
    return true;
  };

  if (ok && p.terrain_fidelity == env::TerrainFidelity::HTILE) {
    auto* tile = dynamic_cast<env::terrain::HtileTile*>(env_.terrain.get());
    std::vector<std::uint8_t> image;
    std::uint64_t specHash = 0;
    if (tile == nullptr) {
      if (auto* log = componentLog(); log != nullptr) {
        log->info(label(), "init: terrain model is not an HtileTile");
      }
      ok = false;
    } else if (!FETCH(wb::WorldEntryRole::TERRAIN, "terrain", image, specHash)) {
      ok = false;
    } else {
      const env::terrain::Status tstatus = tile->loadFromImage(image.data(), image.size());
      if (!env::terrain::isSuccess(tstatus)) {
        if (auto* log = componentLog(); log != nullptr) {
          log->info(label(),
                    fmt::format("init: terrain load failed ({})", env::terrain::toString(tstatus)));
        }
        ok = false;
      } else {
        s.terrain_spec_hash = specHash;
        // Artifact identity for paired runs: both sides of a pairing log
        // the header spec_hash at load, so file agreement is provable
        // from the two logs alone.
        if (auto* log = componentLog(); log != nullptr) {
          const auto& H = tile->header();
          log->info(label(), fmt::format("terrain artifact: body={} spec_hash={:#018x} "
                                         "extent lat [{:.3f}, {:.3f}] lon [{:.3f}, {:.3f}] {}x{}",
                                         std::string(H.body, strnlen(H.body, sizeof(H.body))),
                                         H.spec_hash, H.lat_min_deg, H.lat_max_deg, H.lon_min_deg,
                                         H.lon_max_deg, H.dim_lat, H.dim_lon));
        }
      }
    }
  }
  if (ok && p.atmosphere_fidelity == env::AtmosphereFidelity::LAYERED) {
    auto* atm = dynamic_cast<env::atmosphere::LayeredAtmosphere*>(env_.atmosphere.get());
    std::vector<std::uint8_t> image;
    std::uint64_t specHash = 0;
    if (atm == nullptr) {
      if (auto* log = componentLog(); log != nullptr) {
        log->info(label(), "init: atmosphere model is not a LayeredAtmosphere");
      }
      ok = false;
    } else if (!FETCH(wb::WorldEntryRole::ATMOSPHERE, "atmosphere", image, specHash)) {
      ok = false;
    } else {
      const env::atmosphere::Status astatus = atm->loadFromImage(image.data(), image.size());
      if (!env::atmosphere::isSuccess(astatus)) {
        if (auto* log = componentLog(); log != nullptr) {
          log->info(label(), fmt::format("init: atmosphere load failed ({})",
                                         env::atmosphere::toString(astatus)));
        }
        ok = false;
      } else {
        s.atmosphere_spec_hash = specHash;
        // Artifact identity for paired runs: both sides of a pairing
        // log the header spec_hash at load, so file agreement is
        // provable from the two logs alone (the atmosphere counterpart
        // of the terrain line above).
        if (auto* log = componentLog(); log != nullptr) {
          const auto& H = atm->fileHeader();
          log->info(label(),
                    fmt::format("atmosphere artifact: body={} model=layered "
                                "spec_hash={:#018x} records={} R={:.3f} gamma={:.2f} g0={:.5f}",
                                std::string(H.body, strnlen(H.body, sizeof(H.body))), H.spec_hash,
                                H.n_records, H.R_specific, H.gamma, H.g0));
        }
      }
    }
  }
  // Gravity SPHERICAL fidelity: the coefficient-loading API (a
  // CoeffSource the caller wires + the model's own init()) differs
  // from the terrain/atmosphere in-memory contract, so the entry is
  // required present in the bound world but not loaded here (the
  // gravity-header ticket tracks closing that gap).
  if (ok && p.gravity_fidelity == env::GravityFidelity::SPHERICAL) {
    std::size_t idx = 0;
    if (bundle.findEntry(wb::WorldEntryRole::GRAVITY, idx) != wb::WorldBundleCheck::OK) {
      if (auto* log = componentLog(); log != nullptr) {
        log->info(label(), "init: fidelity requires a gravity entry the bound world "
                           "does not carry");
      }
      ok = false;
    }
  }
  if (ok && needsWorld(p)) {
    s.world_uid = p.world_uid;
    s.world_pin = p.world_pin;
  }

  return ok;
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

  // 3. Bind the world and load file-backed models from its entries.
  //    The binding is the contract (bindWorld): uid+pin discovery in
  //    the bank, refusal naming any miss, entry payloads to the
  //    models' in-memory loaders. Shared with the RELOAD_TPRM rebind
  //    re-entry, where the same contract swaps the running world.
  const bool ok = bindWorld(p, s);

  if (!ok) {
    s.init_status = 2;
    return static_cast<std::uint8_t>(ApexStatus::ERROR_LOAD_INVALID);
  }
  s.data_loaded = 1;

  // 4. Populate OUTPUT telemetry: a one-shot snapshot of body identity
  //    + key physical summary derived from the live env models. This
  //    is the public face other components subscribe to and that an
  //    external bridge reads to forward to visualization / ground systems.
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
