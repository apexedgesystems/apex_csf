#ifndef APEX_SIM_ENVIRONMENT_ATMOSPHERE_LAYERED_HPP
#define APEX_SIM_ENVIRONMENT_ATMOSPHERE_LAYERED_HPP
/**
 * @file LayeredAtmosphere.hpp
 * @brief Hydrostatic piecewise-linear-temperature atmosphere (USSA76-class).
 *
 * Stores N layers, each with (base_alt_m, base_T_K, base_P_Pa,
 * lapse_K_per_m). For an altitude `h` falling within layer `b`:
 *   T(h) = T_b + L * (h - h_b)
 *   P(h) = P_b * (T_b / T(h))^(g0 / (R * L))         if L != 0
 *        = P_b * exp(-g0 * (h - h_b) / (R * T_b))    if L == 0  (isothermal)
 *   rho(h) = P(h) / (R * T(h))
 *   a(h)   = sqrt(gamma * R * T(h))
 *
 * Loads the layer table + thermodynamic constants from a `.atm`
 * file with `model_type == kLayered`. The same class drives Earth's
 * USSA76 atmosphere (7 layers up to 86 km) and any procedural layered
 * atmosphere generated for fictional bodies.
 *
 * @note NOT RT-safe at load(); RT-safe O(log N) for query() (binary
 *       search across layers).
 */

#include "src/sim/environment/atmosphere/inc/Atm.hpp"
#include "src/sim/environment/atmosphere/inc/AtmosphereModelBase.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sim {
namespace environment {
namespace atmosphere {

/* ----------------------------- LayeredAtmosphere ----------------------------- */

class LayeredAtmosphere : public AtmosphereModelBase {
public:
  /// One precomputed layer, derived from the `.atm` records on load.
  struct Layer {
    double base_alt_m;    ///< Base altitude of the layer.
    double base_T_K;      ///< Temperature at the base.
    double base_P_Pa;     ///< Pressure at the base.
    double lapse_K_per_m; ///< dT/dh within the layer (signed).
  };

  LayeredAtmosphere() noexcept = default;
  ~LayeredAtmosphere() override = default;

  LayeredAtmosphere(const LayeredAtmosphere&) = delete;
  LayeredAtmosphere& operator=(const LayeredAtmosphere&) = delete;

  /// Load a layered .atm file. Returns `Status::SUCCESS` on success, or a
  /// specific error: `ERROR_DATA_PATH_INVALID` (open/header fail),
  /// `ERROR_MODEL_TYPE_MISMATCH` (header model_type != kLayered),
  /// `ERROR_FILE_FORMAT_INVALID` (record read fail), or a parameter error
  /// propagated from `initFromMemory`.
  [[nodiscard]] Status load(const std::string& path) noexcept;

  /// Initialize directly from in-memory data (skips file I/O). Useful
  /// for tests and for hardcoded body presets (e.g. Earth USSA76).
  /// Returns `Status::SUCCESS`, or `ERROR_PARAM_LAYERS_EMPTY`,
  /// `ERROR_PARAM_LAYERS_NONMONOTONIC`, `ERROR_PARAM_TEMP_INVALID`,
  /// `ERROR_PARAM_PRESSURE_INVALID`, `ERROR_PARAM_GAS_CONST_INVALID`.
  [[nodiscard]] Status initFromMemory(const std::vector<Layer>& layers, double R_specific,
                                      double gamma, double g0) noexcept;

  /// Free internal buffers; reset to default state.
  void close() noexcept;

  [[nodiscard]] bool isLoaded() const noexcept { return !layers_.empty(); }

  /// The thermo constants this model uses.
  [[nodiscard]] double gasConstant() const noexcept { return R_; }
  [[nodiscard]] double gamma() const noexcept { return gamma_; }
  [[nodiscard]] double surfaceGravity() const noexcept { return g0_; }
  [[nodiscard]] std::size_t numLayers() const noexcept { return layers_.size(); }
  [[nodiscard]] const Layer& layer(std::size_t i) const noexcept { return layers_[i]; }

  /* ----------------------------- AtmosphereModelBase API ----------------------------- */

  [[nodiscard]] Status query(double alt_m, double lat_rad, double lon_rad,
                             AtmosphereState& s) const noexcept override;

  [[nodiscard]] double minAltitudeM() const noexcept override;
  [[nodiscard]] double maxAltitudeM() const noexcept override;

private:
  /// Find index of the layer containing `alt_m`. Clamps to first/last
  /// when out of the table's documented range.
  [[nodiscard]] std::size_t findLayer(double alt_m) const noexcept;

  std::vector<Layer> layers_;
  double R_ = 287.058;  ///< J/(kg*K)
  double gamma_ = 1.4;  ///< cp/cv
  double g0_ = 9.80665; ///< m/s^2
  double max_alt_m_ =
      0.0; ///< Top of the highest layer + an extrapolation cap (default = top + 5 km).
};

} // namespace atmosphere
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_ATMOSPHERE_LAYERED_HPP
