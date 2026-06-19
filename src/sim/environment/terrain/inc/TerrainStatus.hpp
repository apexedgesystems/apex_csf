#ifndef APEX_SIM_ENVIRONMENT_TERRAIN_STATUS_HPP
#define APEX_SIM_ENVIRONMENT_TERRAIN_STATUS_HPP
/**
 * @file TerrainStatus.hpp
 * @brief Compact, strongly-typed status codes for terrain model operations.
 *
 * Conventions (mirror gravity/GravityStatus.hpp):
 *  - SUCCESS = 0.
 *  - Non-errors use plain names (e.g., WARN_*).
 *  - Errors are prefixed with ERROR_*.
 *  - EOE_TERRAIN marks the end of base codes; derivatives extend after it.
 */

#include <cstdint>

namespace sim {
namespace environment {
namespace terrain {

/* ----------------------------- Status ----------------------------- */

/// Status codes for terrain model operations. Used by all terrain model
/// implementations (HtileTile and any future variants).
enum class Status : std::uint8_t {
  // Success -------------------------------------------------------------------
  SUCCESS = 0,

  // Warnings ------------------------------------------------------------------
  WARN_OUTSIDE_COVERAGE, ///< Position outside this tile's lat/lon bounds.
  WARN_VOID_DATA,        ///< Sample at position is the void marker.

  // Initialization errors -----------------------------------------------------
  ERROR_NOT_INITIALIZED,         ///< Model not loaded (load() not called).
  ERROR_DATA_PATH_INVALID,       ///< Tile path does not exist or unreadable.
  ERROR_FILE_FORMAT_INVALID,     ///< Tile header / body fails structural checks.
  ERROR_SAMPLE_TYPE_UNSUPPORTED, ///< Sample type in header not handled by this consumer.
  ERROR_ALLOC_FAIL,              ///< Sample buffer allocation failed.

  // Query errors --------------------------------------------------------------
  ERROR_PARAM_BUFFER_NULL, ///< Output buffer pointer is null.

  // Marker --------------------------------------------------------------------
  EOE_TERRAIN ///< End of enum marker for extensions.
};

/* ----------------------------- API ----------------------------- */

/// Returns a static-string for the status. RT-safe; no allocation.
inline const char* toString(Status s) noexcept {
  switch (s) {
  case Status::SUCCESS:
    return "SUCCESS";
  case Status::WARN_OUTSIDE_COVERAGE:
    return "WARN_OUTSIDE_COVERAGE";
  case Status::WARN_VOID_DATA:
    return "WARN_VOID_DATA";
  case Status::ERROR_NOT_INITIALIZED:
    return "ERROR_NOT_INITIALIZED";
  case Status::ERROR_DATA_PATH_INVALID:
    return "ERROR_DATA_PATH_INVALID";
  case Status::ERROR_FILE_FORMAT_INVALID:
    return "ERROR_FILE_FORMAT_INVALID";
  case Status::ERROR_SAMPLE_TYPE_UNSUPPORTED:
    return "ERROR_SAMPLE_TYPE_UNSUPPORTED";
  case Status::ERROR_ALLOC_FAIL:
    return "ERROR_ALLOC_FAIL";
  case Status::ERROR_PARAM_BUFFER_NULL:
    return "ERROR_PARAM_BUFFER_NULL";
  case Status::EOE_TERRAIN:
    return "EOE_TERRAIN";
  }
  return "UNKNOWN_STATUS";
}

inline bool isSuccess(Status s) noexcept { return s == Status::SUCCESS; }

inline bool isWarning(Status s) noexcept {
  return s == Status::WARN_OUTSIDE_COVERAGE || s == Status::WARN_VOID_DATA;
}

inline bool isError(Status s) noexcept {
  return !isSuccess(s) && !isWarning(s) && s != Status::EOE_TERRAIN;
}

} // namespace terrain
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_TERRAIN_STATUS_HPP
