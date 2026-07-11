/**
 * @file FloorProbe.cpp
 * @brief MCU-floor probe: instantiates the library's public surface; compiled
 *        at C++17 with -fno-exceptions -fno-rtti by apex_add_floor_check so a
 *        hosted-only regression fails every PR build, not the next firmware
 *        build.
 */

#include "src/utilities/math/celestial/inc/Angles.hpp"
#include "src/utilities/math/celestial/inc/EarthConstants.hpp"
#include "src/utilities/math/celestial/inc/MoonConstants.hpp"

namespace cel = apex::math::celestial;

double probe() {
  static_assert(cel::earth::A > cel::earth::B, "oblate");
  static_assert(cel::degToRad(180.0) > 3.14, "conversion is constexpr");
  return cel::earth::OMEGA + cel::moon::OMEGA + cel::degToRad(1.0);
}
