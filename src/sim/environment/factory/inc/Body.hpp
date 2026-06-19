#ifndef APEX_SIM_ENVIRONMENT_BODY_HPP
#define APEX_SIM_ENVIRONMENT_BODY_HPP
/**
 * @file Body.hpp
 * @brief Celestial body identifier shared across the environment subsystem.
 *
 * Used by EnvironmentFactory to dispatch on body when building gravity,
 * terrain, and atmosphere models -- it selects the body-specific defaults
 * (GM, reference radius, ref_surface, sea-level conditions) applied to
 * each model.
 *
 * `OTHER` is the slot for any body without a built-in default
 * (procedural fictional planets, future Mars / Titan / etc.). Models
 * built for `OTHER` are returned uninitialized and the caller is
 * expected to wire the body params themselves.
 */

#include <cstdint>

namespace sim {
namespace environment {

/* ----------------------------- Body ----------------------------- */

/// Celestial body the model is built for. Determines body-specific
/// defaults (GM, reference radius, ref_surface, etc.) inside the factory.
enum class Body : std::uint8_t {
  EARTH = 0,
  MOON = 1,
  OTHER = 2, ///< Caller-defined body; factory returns uninitialized models.
};

/// Returns a static-string for the Body. RT-safe, no allocation.
inline const char* toString(Body b) noexcept {
  switch (b) {
  case Body::EARTH:
    return "EARTH";
  case Body::MOON:
    return "MOON";
  case Body::OTHER:
    return "OTHER";
  }
  return "UNKNOWN_BODY";
}

} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_BODY_HPP
