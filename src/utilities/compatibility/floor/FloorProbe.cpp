/**
 * @file FloorProbe.cpp
 * @brief MCU-floor probe: instantiates the library's public surface; compiled
 *        at C++17 with -fno-exceptions -fno-rtti by apex_add_floor_check so a
 *        hosted-only regression fails every PR build, not the next firmware
 *        build.
 */

#include "src/utilities/compatibility/inc/compat_math.hpp"
#include "src/utilities/compatibility/inc/compat_type_traits.hpp"

float probe() {
  static_assert(apex::compat::is_same_v<float, float>, "traits reachable");
  static_assert(!apex::compat::is_same_v<float, double>, "traits discriminate");
  constexpr float EPS = apex::compat::epsilon<float>();
  return apex::compat::sqrt(2.0f) + apex::compat::atan2(1.0f, 1.0f) + EPS;
}
