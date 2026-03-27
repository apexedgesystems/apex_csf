#ifndef APEX_MATH_QUATERNION_STATUS_HPP
#define APEX_MATH_QUATERNION_STATUS_HPP
/**
 * @file QuaternionStatus.hpp
 * @brief Status codes for Quaternion operations.
 *
 * @note RT-SAFE: All status codes are trivial enums.
 */

#include <cstdint>

namespace apex {
namespace math {
namespace quaternion {

/* -------------------------------- Status ---------------------------------- */

/**
 * @brief Status codes returned by Quaternion operations.
 */
enum class Status : std::uint8_t {
  SUCCESS = 0,          ///< Operation succeeded.
  ERROR_INVALID_VALUE,  ///< Input value is invalid (e.g., zero-norm).
  ERROR_SIZE_MISMATCH,  ///< Array/vector size mismatch.
  ERROR_SINGULAR,       ///< Singular or degenerate input.
  ERROR_NOT_NORMALIZED, ///< Quaternion is not unit quaternion.
  ERROR_UNSUPPORTED_OP, ///< Operation not supported.
  ERROR_UNKNOWN         ///< Unknown error.
};

} // namespace quaternion
} // namespace math
} // namespace apex

#endif // APEX_MATH_QUATERNION_STATUS_HPP
