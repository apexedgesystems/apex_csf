#ifndef APEX_SIM_ENVIRONMENT_GRAVITY_STATUS_HPP
#define APEX_SIM_ENVIRONMENT_GRAVITY_STATUS_HPP
/**
 * @file GravityStatus.hpp
 * @brief Compact, strongly-typed status codes for gravity model operations.
 *
 * Conventions:
 *  - SUCCESS = 0.
 *  - Non-errors use plain names (e.g., WARN_*).
 *  - Errors are prefixed with ERROR_*.
 *  - EOE_GRAVITY marks the end of base codes; derivatives extend after it.
 */

#include <cstdint>

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- Status ----------------------------- */

/**
 * @enum Status
 * @brief Status codes for gravity model operations.
 *
 * Used by all gravity model classes (SphericalHarmonicModel, J2Model, etc.).
 */
enum class Status : std::uint8_t {
  // Success -------------------------------------------------------------------
  SUCCESS = 0, ///< Operation completed successfully.

  // Warnings ------------------------------------------------------------------
  WARN_BELOW_SURFACE,  ///< Position below reference surface (result may be inaccurate).
  WARN_DEGREE_CLAMPED, ///< Requested degree clamped to available max.

  // Initialization errors -----------------------------------------------------
  ERROR_NOT_INITIALIZED,      ///< Model not initialized (init() not called).
  ERROR_DATA_PATH_INVALID,    ///< Coefficient file path does not exist.
  ERROR_COEFF_FILE_INVALID,   ///< Coefficient file format invalid or corrupted.
  ERROR_COEFF_LOAD_FAIL,      ///< Failed to load coefficients.
  ERROR_PARAM_GM_INVALID,     ///< GM (gravitational parameter) <= 0.
  ERROR_PARAM_RADIUS_INVALID, ///< Reference radius <= 0.
  ERROR_PARAM_DEGREE_INVALID, ///< Requested degree < 0.
  ERROR_ALLOC_FAIL,           ///< Failed to allocate scratch buffers.

  // Query errors --------------------------------------------------------------
  ERROR_POSITION_ZERO,     ///< Position magnitude is zero (singularity).
  ERROR_POSITION_NAN,      ///< Position contains NaN values.
  ERROR_PARAM_BUFFER_NULL, ///< Output buffer pointer is null.

  // Coefficient source errors -------------------------------------------------
  ERROR_SOURCE_NULL,            ///< Coefficient source pointer is null.
  ERROR_SOURCE_DEGREE_MISMATCH, ///< Source max degree less than requested.

  // Marker --------------------------------------------------------------------
  EOE_GRAVITY ///< End of enum marker for extensions.
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Human-readable string for Status.
 * @param s Status code.
 * @return Static string (no allocation).
 * @note RT-safe: Returns pointer to static string literal.
 */
inline const char* toString(Status s) noexcept {
  switch (s) {
  case Status::SUCCESS:
    return "SUCCESS";
  case Status::WARN_BELOW_SURFACE:
    return "WARN_BELOW_SURFACE";
  case Status::WARN_DEGREE_CLAMPED:
    return "WARN_DEGREE_CLAMPED";
  case Status::ERROR_NOT_INITIALIZED:
    return "ERROR_NOT_INITIALIZED";
  case Status::ERROR_DATA_PATH_INVALID:
    return "ERROR_DATA_PATH_INVALID";
  case Status::ERROR_COEFF_FILE_INVALID:
    return "ERROR_COEFF_FILE_INVALID";
  case Status::ERROR_COEFF_LOAD_FAIL:
    return "ERROR_COEFF_LOAD_FAIL";
  case Status::ERROR_PARAM_GM_INVALID:
    return "ERROR_PARAM_GM_INVALID";
  case Status::ERROR_PARAM_RADIUS_INVALID:
    return "ERROR_PARAM_RADIUS_INVALID";
  case Status::ERROR_PARAM_DEGREE_INVALID:
    return "ERROR_PARAM_DEGREE_INVALID";
  case Status::ERROR_ALLOC_FAIL:
    return "ERROR_ALLOC_FAIL";
  case Status::ERROR_POSITION_ZERO:
    return "ERROR_POSITION_ZERO";
  case Status::ERROR_POSITION_NAN:
    return "ERROR_POSITION_NAN";
  case Status::ERROR_PARAM_BUFFER_NULL:
    return "ERROR_PARAM_BUFFER_NULL";
  case Status::ERROR_SOURCE_NULL:
    return "ERROR_SOURCE_NULL";
  case Status::ERROR_SOURCE_DEGREE_MISMATCH:
    return "ERROR_SOURCE_DEGREE_MISMATCH";
  case Status::EOE_GRAVITY:
    return "EOE_GRAVITY";
  }
  return "UNKNOWN_STATUS";
}

/**
 * @brief Check if status indicates success.
 * @param s Status code.
 * @return true if SUCCESS.
 * @note RT-safe: O(1).
 */
inline bool isSuccess(Status s) noexcept { return s == Status::SUCCESS; }

/**
 * @brief Check if status is a warning (not error).
 * @param s Status code.
 * @return true if warning code.
 * @note RT-safe: O(1).
 */
inline bool isWarning(Status s) noexcept {
  return s == Status::WARN_BELOW_SURFACE || s == Status::WARN_DEGREE_CLAMPED;
}

/**
 * @brief Check if status indicates an error.
 * @param s Status code.
 * @return true if error code.
 * @note RT-safe: O(1).
 */
inline bool isError(Status s) noexcept {
  return !isSuccess(s) && !isWarning(s) && s != Status::EOE_GRAVITY;
}

} // namespace gravity
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_GRAVITY_STATUS_HPP
