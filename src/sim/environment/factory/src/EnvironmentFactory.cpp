/**
 * @file EnvironmentFactory.cpp
 * @brief Implementation of the gravity + terrain + atmosphere factory dispatch.
 */

#include "src/sim/environment/factory/inc/EnvironmentFactory.hpp"

#include "src/sim/environment/gravity/inc/ConstantGravityModel.hpp"
#include "src/sim/environment/gravity/inc/J2GravityModel.hpp"
#include "src/sim/environment/gravity/inc/SphericalHarmonicModel.hpp"
#include "src/sim/environment/gravity/inc/earth/Wgs84Constants.hpp"
#include "src/sim/environment/gravity/inc/moon/LunarConstants.hpp"

#include "src/sim/environment/terrain/inc/ConstantTerrain.hpp"
#include "src/sim/environment/terrain/inc/EllipsoidTerrain.hpp"
#include "src/sim/environment/terrain/inc/HtileTile.hpp"
#include "src/sim/environment/terrain/inc/SphereTerrain.hpp"
#include "src/sim/environment/terrain/inc/earth/SrtmTerrainModel.hpp"
#include "src/sim/environment/terrain/inc/earth/Wgs84TerrainConstants.hpp"
#include "src/sim/environment/terrain/inc/moon/LolaTerrainModel.hpp"
#include "src/sim/environment/terrain/inc/moon/LunarTerrainConstants.hpp"

#include "src/sim/environment/atmosphere/inc/ConstantAtmosphere.hpp"
#include "src/sim/environment/atmosphere/inc/ExponentialAtmosphere.hpp"
#include "src/sim/environment/atmosphere/inc/LayeredAtmosphere.hpp"
#include "src/sim/environment/atmosphere/inc/earth/Ussa76AtmosphereModel.hpp"
#include "src/sim/environment/atmosphere/inc/moon/VacuumAtmosphereModel.hpp"

namespace sim {
namespace environment {

/* ----------------------------- Body defaults ----------------------------- */

namespace {

/// Per-body un-normalized J2 magnitudes, consistent with the C20 zonal
/// coefficients used by each body's gravity field model: Earth from EGM2008
/// (J2 = -sqrt(5) * C20, gravity::wgs84::egm2008::C20) and Moon from GRGM1200A
/// (gravity::lunar::grgm1200a::C20). Kept as local literals rather than the
/// gravity headers' standalone J2 constants because those standalone values
/// are rounded and differ slightly from the C20-derived figures; these match
/// the models' own coefficients to six significant digits.
constexpr double EARTH_J2 = 1.0826266835e-3;
constexpr double MOON_J2 = 2.0321568464e-4;

/// Per-body surface gravity for ConstantGravityModel.
constexpr double EARTH_G0 = 9.80665;
constexpr double MOON_G0 = 1.62519;

} // namespace

/* ----------------------------- Gravity ----------------------------- */

std::unique_ptr<gravity::GravityModelBase> makeGravityModel(Body body, GravityFidelity fidelity) {
  switch (fidelity) {
  case GravityFidelity::CONSTANT: {
    double g0 = gravity::DEFAULT_G0; // sea-level Earth value as a safe default
    if (body == Body::EARTH)
      g0 = EARTH_G0;
    else if (body == Body::MOON)
      g0 = MOON_G0;
    return std::make_unique<gravity::ConstantGravityModel>(g0);
  }
  case GravityFidelity::J2: {
    auto m = std::make_unique<gravity::J2GravityModel>();
    gravity::J2Params p{};
    if (body == Body::EARTH) {
      p.GM = gravity::wgs84::GM;
      p.a = gravity::wgs84::A;
      p.J2 = EARTH_J2;
      (void)m->init(p);
    } else if (body == Body::MOON) {
      p.GM = gravity::lunar::GM;
      p.a = gravity::lunar::R_REF;
      p.J2 = MOON_J2;
      (void)m->init(p);
    }
    // OTHER: returned uninitialized; caller must call init() with their own params.
    return m;
  }
  case GravityFidelity::SPHERICAL:
    // SphericalHarmonicModel needs a CoeffSource; caller initializes.
    return std::make_unique<gravity::SphericalHarmonicModel>();
  }
  return nullptr;
}

/* ----------------------------- Terrain ----------------------------- */

std::unique_ptr<terrain::TerrainModelBase> makeTerrainModel(Body body, TerrainFidelity fidelity) {
  switch (fidelity) {
  case TerrainFidelity::CONSTANT:
    // Reference height = 0 always; body has no effect on this analytic model.
    return std::make_unique<terrain::ConstantTerrain>(0.0);

  case TerrainFidelity::SPHERE: {
    double r = 1.0;
    if (body == Body::EARTH)
      r = terrain::earth::wgs84::R_EQ_M;
    else if (body == Body::MOON)
      r = terrain::moon::lunar::R_REF_M;
    return std::make_unique<terrain::SphereTerrain>(r);
  }

  case TerrainFidelity::ELLIPSOID: {
    double rEq = 1.0;
    double rPol = 1.0;
    if (body == Body::EARTH) {
      rEq = terrain::earth::wgs84::R_EQ_M;
      rPol = terrain::earth::wgs84::R_POL_M;
    } else if (body == Body::MOON) {
      // Moon is treated as a sphere geometrically; rEq == rPol.
      rEq = terrain::moon::lunar::R_REF_M;
      rPol = terrain::moon::lunar::R_REF_M;
    }
    return std::make_unique<terrain::EllipsoidTerrain>(rEq, rPol);
  }

  case TerrainFidelity::HTILE:
    if (body == Body::EARTH) {
      return std::make_unique<terrain::earth::SrtmTerrainModel>();
    }
    if (body == Body::MOON) {
      return std::make_unique<terrain::moon::LolaTerrainModel>();
    }
    // OTHER: generic htile consumer; caller calls load() with their own path.
    return std::make_unique<terrain::HtileTile>();
  }
  return nullptr;
}

/* ----------------------------- Atmosphere ----------------------------- */

std::unique_ptr<atmosphere::AtmosphereModelBase> makeAtmosphereModel(Body body,
                                                                     AtmosphereFidelity fidelity) {
  switch (fidelity) {
  case AtmosphereFidelity::CONSTANT: {
    if (body == Body::EARTH) {
      // Sea-level ISA values held constant. Seldom what callers actually
      // want past first-cut prototyping, but it's a sensible Earth default.
      return std::make_unique<atmosphere::ConstantAtmosphere>(1.225, 288.15, 101325.0);
    }
    // MOON + OTHER: vacuum.
    return std::make_unique<atmosphere::ConstantAtmosphere>();
  }
  case AtmosphereFidelity::EXPONENTIAL: {
    if (body == Body::EARTH) {
      // Earth tropospheric defaults are baked into ExponentialAtmosphere.
      return std::make_unique<atmosphere::ExponentialAtmosphere>();
    }
    if (body == Body::MOON) {
      // Even at exponential fidelity, the Moon is vacuum for sim purposes.
      return std::make_unique<atmosphere::moon::VacuumAtmosphereModel>();
    }
    // OTHER: returned with default Earth params; caller may re-init().
    return std::make_unique<atmosphere::ExponentialAtmosphere>();
  }
  case AtmosphereFidelity::LAYERED: {
    if (body == Body::EARTH) {
      // USSA76 wrapper bakes in the 7-layer table + dry-air gas constants.
      return std::make_unique<atmosphere::earth::Ussa76AtmosphereModel>();
    }
    if (body == Body::MOON) {
      // No useful layered atmosphere for the Moon -- return vacuum sentinel.
      return std::make_unique<atmosphere::moon::VacuumAtmosphereModel>();
    }
    // OTHER: returned uninitialized; caller calls load() with their .atm path.
    return std::make_unique<atmosphere::LayeredAtmosphere>();
  }
  case AtmosphereFidelity::EMPIRICAL:
    // Not implemented yet (NRLMSISE-00 and friends).
    return nullptr;
  }
  return nullptr;
}

/* ----------------------------- Bundled ----------------------------- */

EnvironmentModels makeEnvironment(const EnvironmentSpec& spec) {
  return EnvironmentModels{makeGravityModel(spec.body, spec.gravity),
                           makeTerrainModel(spec.body, spec.terrain),
                           makeAtmosphereModel(spec.body, spec.atmosphere)};
}

} // namespace environment
} // namespace sim
