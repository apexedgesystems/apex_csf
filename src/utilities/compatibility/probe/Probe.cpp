/**
 * @file Probe.cpp
 * @brief Compile probe: instantiates the library's public surface. Built on
 *        every platform the lib.manifest declares — under each cross
 *        toolchain on MCU builds, at each declared posix_cpp dialect on
 *        hosted builds — so a regression against the support contract fails
 *        the build that owns the claim.
 */

#include "src/utilities/compatibility/inc/compat_math.hpp"
#include "src/utilities/compatibility/inc/compat_type_traits.hpp"

float probe() {
  static_assert(apex::compat::is_same_v<float, float>, "traits reachable");
  static_assert(!apex::compat::is_same_v<float, double>, "traits discriminate");
  constexpr float EPS = apex::compat::epsilon<float>();
  return apex::compat::sqrt(2.0f) + apex::compat::atan2(1.0f, 1.0f) + EPS;
}
