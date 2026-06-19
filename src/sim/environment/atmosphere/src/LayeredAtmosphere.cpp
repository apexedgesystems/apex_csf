/**
 * @file LayeredAtmosphere.cpp
 * @brief Implementation of the hydrostatic piecewise-layered atmosphere.
 */

#include "src/sim/environment/atmosphere/inc/LayeredAtmosphere.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sim {
namespace environment {
namespace atmosphere {

/* ----------------------------- Constants ----------------------------- */

namespace {
/// How far above the top layer's base we'll still answer queries (0 means
/// strictly limit to the table; we choose a small extrapolation tolerance
/// so calls right above the top layer aren't rejected outright).
constexpr double EXTRAPOLATION_TOLERANCE_M = 5000.0;
} // namespace

/* ----------------------------- LayeredAtmosphere Methods ----------------------------- */

Status LayeredAtmosphere::load(const std::string& path) noexcept {
  AtmReader reader;
  if (!reader.open(path.c_str())) {
    // open() fails on a missing/unreadable file or any header-structure
    // failure; both surface as a data-path error to the caller.
    return Status::ERROR_DATA_PATH_INVALID;
  }
  const AtmHeader& H = reader.header();
  if (H.model_type != static_cast<std::uint8_t>(AtmModelType::kLayered)) {
    reader.close();
    return Status::ERROR_MODEL_TYPE_MISMATCH;
  }
  const std::size_t N = H.n_records;
  std::vector<AtmRecord> records(N);
  if (!reader.readAllRecords(records.data(), N)) {
    reader.close();
    return Status::ERROR_FILE_FORMAT_INVALID;
  }
  reader.close();

  std::vector<Layer> layers(N);
  for (std::size_t i = 0; i < N; ++i) {
    layers[i].base_alt_m = records[i].f0;
    layers[i].base_T_K = records[i].f1;
    layers[i].base_P_Pa = records[i].f2;
    layers[i].lapse_K_per_m = records[i].f3;
  }
  return initFromMemory(layers, H.R_specific, H.gamma, H.g0);
}

Status LayeredAtmosphere::initFromMemory(const std::vector<Layer>& layers, double R_specific,
                                         double gamma, double g0) noexcept {
  if (layers.empty()) {
    return Status::ERROR_PARAM_LAYERS_EMPTY;
  }
  // R_specific, gamma, and g0 are the model's thermodynamic constants; an
  // out-of-range value in any of them makes the hydrostatic integration
  // meaningless, so they share the gas-constant error code.
  if (R_specific <= 0.0 || gamma <= 1.0 || g0 <= 0.0) {
    return Status::ERROR_PARAM_GAS_CONST_INVALID;
  }
  // Strictly increasing base altitudes + positive temperatures.
  for (std::size_t i = 0; i < layers.size(); ++i) {
    if (layers[i].base_T_K <= 0.0) {
      return Status::ERROR_PARAM_TEMP_INVALID;
    }
    if (layers[i].base_P_Pa < 0.0) {
      return Status::ERROR_PARAM_PRESSURE_INVALID;
    }
    if (i > 0 && layers[i].base_alt_m <= layers[i - 1].base_alt_m) {
      return Status::ERROR_PARAM_LAYERS_NONMONOTONIC;
    }
  }
  layers_ = layers;
  R_ = R_specific;
  gamma_ = gamma;
  g0_ = g0;
  max_alt_m_ = layers_.back().base_alt_m + EXTRAPOLATION_TOLERANCE_M;
  return Status::SUCCESS;
}

void LayeredAtmosphere::close() noexcept {
  layers_.clear();
  R_ = 287.058;
  gamma_ = 1.4;
  g0_ = 9.80665;
  max_alt_m_ = 0.0;
}

std::size_t LayeredAtmosphere::findLayer(double alt_m) const noexcept {
  // Clamp to first layer for altitudes below the table, last layer above.
  if (alt_m <= layers_.front().base_alt_m) {
    return 0;
  }
  if (alt_m >= layers_.back().base_alt_m) {
    return layers_.size() - 1;
  }
  // Binary search: find largest index i where layers_[i].base_alt_m <= alt_m.
  std::size_t lo = 0;
  std::size_t hi = layers_.size() - 1;
  while (lo < hi) {
    const std::size_t MID = lo + (hi - lo + 1) / 2;
    if (layers_[MID].base_alt_m <= alt_m) {
      lo = MID;
    } else {
      hi = MID - 1;
    }
  }
  return lo;
}

Status LayeredAtmosphere::query(double alt_m, double /*lat_rad*/, double /*lon_rad*/,
                                AtmosphereState& s) const noexcept {
  if (layers_.empty()) {
    return Status::ERROR_NOT_INITIALIZED;
  }
  if (std::isnan(alt_m)) {
    return Status::ERROR_PARAM_ALT_INVALID;
  }
  if (alt_m > max_alt_m_) {
    // Above the documented table + extrapolation band: leave `s` unmodified.
    return Status::WARN_OUT_OF_VALID_RANGE;
  }

  const std::size_t I = findLayer(alt_m);
  const Layer& L = layers_[I];
  const double DH = alt_m - L.base_alt_m;

  // Temperature: linear within the layer.
  const double T = L.base_T_K + L.lapse_K_per_m * DH;
  if (T <= 0.0) {
    // Numerically degenerate (would happen only with grossly wrong inputs).
    return Status::ERROR_PARAM_TEMP_INVALID;
  }

  // Pressure: hydrostatic integration. Two cases per USSA76 conventions.
  double P;
  if (L.lapse_K_per_m == 0.0) {
    // Isothermal layer: P = P_b * exp(-g0 * dh / (R * T_b))
    P = L.base_P_Pa * std::exp(-g0_ * DH / (R_ * L.base_T_K));
  } else {
    // Non-isothermal layer: P = P_b * (T_b / T)^(g0 / (R * L))
    P = L.base_P_Pa * std::pow(L.base_T_K / T, g0_ / (R_ * L.lapse_K_per_m));
  }

  s.T = T;
  s.P = P;
  s.rho = P / (R_ * T);             // ideal gas law
  s.a = std::sqrt(gamma_ * R_ * T); // speed of sound
  return Status::SUCCESS;
}

double LayeredAtmosphere::minAltitudeM() const noexcept {
  return layers_.empty() ? 0.0 : layers_.front().base_alt_m;
}
double LayeredAtmosphere::maxAltitudeM() const noexcept {
  return layers_.empty() ? 0.0 : max_alt_m_;
}

} // namespace atmosphere
} // namespace environment
} // namespace sim
