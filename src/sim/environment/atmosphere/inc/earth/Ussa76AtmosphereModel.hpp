#ifndef APEX_SIM_ENVIRONMENT_ATMOSPHERE_EARTH_USSA76_HPP
#define APEX_SIM_ENVIRONMENT_ATMOSPHERE_EARTH_USSA76_HPP
/**
 * @file Ussa76AtmosphereModel.hpp
 * @brief Earth USSA76 atmosphere model (LayeredAtmosphere with hardcoded layers).
 *
 * Earth-specific wrapper around LayeredAtmosphere that bakes in the 7-layer
 * USSA76 table and the dry-air gas constants. Default-constructed instances
 * are READY-TO-USE -- no `load()` or `init()` needed.
 *
 * Mirrors `terrain/earth/SrtmTerrainModel.hpp` and
 * `gravity/earth/Egm2008Model.hpp`: an Earth wrapper that fills in body
 * defaults so callers don't have to.
 *
 * If you want to load a procedural Earth-flavored .atm instead of the
 * hardcoded USSA76, construct a `LayeredAtmosphere` directly and call
 * `load(path)`.
 */

#include "src/sim/environment/atmosphere/inc/LayeredAtmosphere.hpp"
#include "src/sim/environment/atmosphere/inc/earth/Ussa76Constants.hpp"

#include <vector>

namespace sim {
namespace environment {
namespace atmosphere {
namespace earth {

/* ----------------------------- Ussa76AtmosphereModel ----------------------------- */

class Ussa76AtmosphereModel final : public LayeredAtmosphere {
public:
  /// Default construction populates the layer table + thermo constants.
  Ussa76AtmosphereModel() noexcept {
    std::vector<LayeredAtmosphere::Layer> layers;
    layers.reserve(earth::NUM_LAYERS);
    for (std::size_t i = 0; i < earth::NUM_LAYERS; ++i) {
      layers.push_back({earth::LAYERS[i].base_alt_m, earth::LAYERS[i].base_T_K,
                        earth::LAYERS[i].base_P_Pa, earth::LAYERS[i].lapse_K_per_m});
    }
    (void)initFromMemory(layers, earth::R_SPECIFIC, earth::GAMMA, earth::G0);
  }
};

} // namespace earth
} // namespace atmosphere
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_ATMOSPHERE_EARTH_USSA76_HPP
