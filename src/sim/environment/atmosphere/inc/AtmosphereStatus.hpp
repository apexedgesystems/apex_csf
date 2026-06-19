#ifndef APEX_SIM_ENVIRONMENT_ATMOSPHERE_STATUS_HPP
#define APEX_SIM_ENVIRONMENT_ATMOSPHERE_STATUS_HPP
/**
 * @file AtmosphereStatus.hpp
 * @brief Compact, strongly-typed status codes for atmosphere model operations.
 *
 * Conventions (mirror gravity/GravityStatus.hpp + terrain/TerrainStatus.hpp):
 *  - SUCCESS = 0.
 *  - Non-errors use plain names (e.g., WARN_*).
 *  - Errors are prefixed with ERROR_*.
 *  - EOE_ATMOSPHERE marks the end of base codes; derivatives extend after it.
 */

#include <cstdint>

namespace sim {
namespace environment {
namespace atmosphere {

/* ----------------------------- Status ----------------------------- */

/// Status codes for atmosphere model operations. Used by all atmosphere
/// model implementations (Constant, Exponential, Layered, etc.).
enum class Status : std::uint8_t {
  // Success -------------------------------------------------------------------
  SUCCESS = 0,

  // Warnings ------------------------------------------------------------------
  WARN_OUT_OF_VALID_RANGE, ///< Altitude outside model's documented validity.
  WARN_VACUUM_QUERY,       ///< Query against a vacuum model (rho == 0 returned).

  // Initialization errors -----------------------------------------------------
  ERROR_NOT_INITIALIZED,           ///< Model not loaded/initialized.
  ERROR_DATA_PATH_INVALID,         ///< .atm file path does not exist or unreadable.
  ERROR_FILE_FORMAT_INVALID,       ///< .atm header / payload fails structural checks.
  ERROR_MODEL_TYPE_MISMATCH,       ///< File's model_type does not match this consumer.
  ERROR_PARAM_RHO_INVALID,         ///< Density parameter < 0.
  ERROR_PARAM_TEMP_INVALID,        ///< Temperature parameter <= 0 (Kelvin).
  ERROR_PARAM_PRESSURE_INVALID,    ///< Pressure parameter < 0.
  ERROR_PARAM_SCALE_INVALID,       ///< Scale height H <= 0 (exponential).
  ERROR_PARAM_GAS_CONST_INVALID,   ///< Specific gas constant R <= 0.
  ERROR_PARAM_LAYERS_EMPTY,        ///< Layered model with no layers.
  ERROR_PARAM_LAYERS_NONMONOTONIC, ///< Layer base altitudes not strictly increasing.
  ERROR_ALLOC_FAIL,                ///< Buffer allocation failed.

  // Query errors --------------------------------------------------------------
  ERROR_PARAM_BUFFER_NULL, ///< Output buffer pointer is null.
  ERROR_PARAM_ALT_INVALID, ///< Altitude is NaN or extremely out of range.

  // Marker --------------------------------------------------------------------
  EOE_ATMOSPHERE ///< End of enum marker for extensions.
};

/* ----------------------------- API ----------------------------- */

/// Returns a static-string for the status. RT-safe; no allocation.
inline const char* toString(Status s) noexcept {
  switch (s) {
  case Status::SUCCESS:
    return "SUCCESS";
  case Status::WARN_OUT_OF_VALID_RANGE:
    return "WARN_OUT_OF_VALID_RANGE";
  case Status::WARN_VACUUM_QUERY:
    return "WARN_VACUUM_QUERY";
  case Status::ERROR_NOT_INITIALIZED:
    return "ERROR_NOT_INITIALIZED";
  case Status::ERROR_DATA_PATH_INVALID:
    return "ERROR_DATA_PATH_INVALID";
  case Status::ERROR_FILE_FORMAT_INVALID:
    return "ERROR_FILE_FORMAT_INVALID";
  case Status::ERROR_MODEL_TYPE_MISMATCH:
    return "ERROR_MODEL_TYPE_MISMATCH";
  case Status::ERROR_PARAM_RHO_INVALID:
    return "ERROR_PARAM_RHO_INVALID";
  case Status::ERROR_PARAM_TEMP_INVALID:
    return "ERROR_PARAM_TEMP_INVALID";
  case Status::ERROR_PARAM_PRESSURE_INVALID:
    return "ERROR_PARAM_PRESSURE_INVALID";
  case Status::ERROR_PARAM_SCALE_INVALID:
    return "ERROR_PARAM_SCALE_INVALID";
  case Status::ERROR_PARAM_GAS_CONST_INVALID:
    return "ERROR_PARAM_GAS_CONST_INVALID";
  case Status::ERROR_PARAM_LAYERS_EMPTY:
    return "ERROR_PARAM_LAYERS_EMPTY";
  case Status::ERROR_PARAM_LAYERS_NONMONOTONIC:
    return "ERROR_PARAM_LAYERS_NONMONOTONIC";
  case Status::ERROR_ALLOC_FAIL:
    return "ERROR_ALLOC_FAIL";
  case Status::ERROR_PARAM_BUFFER_NULL:
    return "ERROR_PARAM_BUFFER_NULL";
  case Status::ERROR_PARAM_ALT_INVALID:
    return "ERROR_PARAM_ALT_INVALID";
  case Status::EOE_ATMOSPHERE:
    return "EOE_ATMOSPHERE";
  }
  return "UNKNOWN_STATUS";
}

inline bool isSuccess(Status s) noexcept { return s == Status::SUCCESS; }

inline bool isWarning(Status s) noexcept {
  return s == Status::WARN_OUT_OF_VALID_RANGE || s == Status::WARN_VACUUM_QUERY;
}

inline bool isError(Status s) noexcept {
  return !isSuccess(s) && !isWarning(s) && s != Status::EOE_ATMOSPHERE;
}

} // namespace atmosphere
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_ATMOSPHERE_STATUS_HPP
