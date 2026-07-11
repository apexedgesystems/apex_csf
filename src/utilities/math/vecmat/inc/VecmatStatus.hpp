#ifndef APEX_MATH_VECMAT_STATUS_HPP
#define APEX_MATH_VECMAT_STATUS_HPP
/**
 * @file VecmatStatus.hpp
 * @brief Status codes for the fixed-size vector/matrix operations.
 *
 * Conventions (mirror ArrayStatus.hpp): SUCCESS = 0; errors < 128. Returned
 * as uint8_t for consistency with the other math modules.
 *
 * @note RT-SAFE: Trivial enum, no allocation.
 */

#include <stdint.h>

namespace apex {
namespace math {
namespace vecmat {

enum class Status : uint8_t {
  SUCCESS = 0,
  ERROR_SINGULAR = 1,      ///< matrix inverse with |det| below the guard
  ERROR_INVALID_VALUE = 2, ///< zero-norm normalize
  ERROR_UNKNOWN = 255
};

inline constexpr bool ok(Status s) noexcept { return s == Status::SUCCESS; }
inline constexpr bool failed(Status s) noexcept { return s != Status::SUCCESS; }

} // namespace vecmat
} // namespace math
} // namespace apex

#endif // APEX_MATH_VECMAT_STATUS_HPP
