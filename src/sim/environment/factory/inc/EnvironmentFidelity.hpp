#ifndef APEX_SIM_ENVIRONMENT_FIDELITY_HPP
#define APEX_SIM_ENVIRONMENT_FIDELITY_HPP
/**
 * @file EnvironmentFidelity.hpp
 * @brief Fidelity-level enums for the environment subsystem.
 *
 * Per-domain fidelity ladders. Used by EnvironmentFactory to dispatch
 * which model type to build:
 *
 *   GravityFidelity     CONSTANT < J2 < SPHERICAL
 *   TerrainFidelity     CONSTANT < SPHERE < ELLIPSOID < HTILE
 *   AtmosphereFidelity  CONSTANT < EXPONENTIAL < LAYERED < EMPIRICAL
 *
 * The ladders are not directly comparable (gravity J2 is different
 * physics from terrain SPHERE) but each is monotonic within its
 * domain: higher-numbered options are more accurate / more expensive.
 */

#include <cstdint>

namespace sim {
namespace environment {

/* ----------------------------- GravityFidelity ----------------------------- */

enum class GravityFidelity : std::uint8_t {
  /// `ConstantGravityModel` -- O(1), no body params.
  CONSTANT = 0,

  /// `J2GravityModel` -- O(1), needs body GM + R + J2.
  J2 = 1,

  /// `SphericalHarmonicModel` -- O(N^2), needs CoeffSource init by caller.
  SPHERICAL = 2,
};

/* ----------------------------- TerrainFidelity ----------------------------- */

enum class TerrainFidelity : std::uint8_t {
  /// `ConstantTerrain` -- flat reference surface everywhere.
  CONSTANT = 0,

  /// `SphereTerrain` -- spherical body, no DEM detail.
  SPHERE = 1,

  /// `EllipsoidTerrain` -- oblate spheroid, no DEM detail.
  ELLIPSOID = 2,

  /// `HtileTile` (or body wrapper) -- full DEM, needs `load(path)` by caller.
  HTILE = 3,
};

/* ----------------------------- AtmosphereFidelity ----------------------------- */

enum class AtmosphereFidelity : std::uint8_t {
  /// `ConstantAtmosphere` -- vacuum default; or any held-fixed (rho, T, P).
  CONSTANT = 0,

  /// `ExponentialAtmosphere` -- rho(h) = rho0 * exp(-h/H), isothermal.
  EXPONENTIAL = 1,

  /// `LayeredAtmosphere` -- USSA76-class hydrostatic layers, needs `load(path)`
  /// for OTHER (Earth/Moon are returned ready via body wrappers).
  LAYERED = 2,

  /// Reserved slot for empirical models (NRLMSISE-00 et al). Factory returns
  /// nullptr until this is implemented.
  EMPIRICAL = 3,
};

/* ----------------------------- Pretty-print ----------------------------- */

inline const char* toString(GravityFidelity f) noexcept {
  switch (f) {
  case GravityFidelity::CONSTANT:
    return "CONSTANT";
  case GravityFidelity::J2:
    return "J2";
  case GravityFidelity::SPHERICAL:
    return "SPHERICAL";
  }
  return "UNKNOWN_GRAVITY_FIDELITY";
}

inline const char* toString(TerrainFidelity f) noexcept {
  switch (f) {
  case TerrainFidelity::CONSTANT:
    return "CONSTANT";
  case TerrainFidelity::SPHERE:
    return "SPHERE";
  case TerrainFidelity::ELLIPSOID:
    return "ELLIPSOID";
  case TerrainFidelity::HTILE:
    return "HTILE";
  }
  return "UNKNOWN_TERRAIN_FIDELITY";
}

inline const char* toString(AtmosphereFidelity f) noexcept {
  switch (f) {
  case AtmosphereFidelity::CONSTANT:
    return "CONSTANT";
  case AtmosphereFidelity::EXPONENTIAL:
    return "EXPONENTIAL";
  case AtmosphereFidelity::LAYERED:
    return "LAYERED";
  case AtmosphereFidelity::EMPIRICAL:
    return "EMPIRICAL";
  }
  return "UNKNOWN_ATMOSPHERE_FIDELITY";
}

} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_FIDELITY_HPP
