#ifndef APEX_MATH_FRAMES_STATUS_HPP
#define APEX_MATH_FRAMES_STATUS_HPP
/**
 * @file FramesStatus.hpp
 * @brief Status codes for the frames library.
 *
 * Conventions (mirror ArrayStatus.hpp): SUCCESS = 0; errors < 128. Returned
 * as uint8_t for consistency with the other math modules. The graph codes
 * cover the fixed-capacity registry and path resolution; the transform codes
 * cover degenerate inputs.
 *
 * @note RT-SAFE: Trivial enum, no allocation.
 */

#include <stdint.h>

namespace apex {
namespace math {
namespace frames {

enum class Status : uint8_t {
  SUCCESS = 0,
  ERROR_INVALID_VALUE = 1, ///< degenerate input (zero-norm rotation)
  ERROR_CAPACITY = 2,      ///< frame registry full (compile-time max)
  ERROR_BAD_FRAME = 3,     ///< FrameId out of range or unregistered
  ERROR_NO_PATH = 4,       ///< frames share no common ancestor
  ERROR_NO_PROVIDER = 5,   ///< time/state-driven edge with a null delegate
  ERROR_UNKNOWN = 255
};

inline constexpr bool ok(Status s) noexcept { return s == Status::SUCCESS; }
inline constexpr bool failed(Status s) noexcept { return s != Status::SUCCESS; }

} // namespace frames
} // namespace math
} // namespace apex

#endif // APEX_MATH_FRAMES_STATUS_HPP
