/**
 * @file FloorProbe.cpp
 * @brief MCU-floor probe: instantiates the library's public surface; compiled
 *        at C++17 with -fno-exceptions -fno-rtti by apex_add_floor_check so a
 *        hosted-only regression fails every PR build, not the next firmware
 *        build.
 */

#include "src/utilities/math/frames/inc/FramesStatus.hpp"
#include "src/utilities/math/frames/inc/Transform.hpp"

namespace fr = apex::math::frames;

float probe() {
  fr::Transform<float> a, b, ab, inv;
  a.rotation().setFromAngleAxis(0.5f, 0.0f, 0.0f, 1.0f);
  a.t[0] = 2.0f;
  (void)fr::composeInto(a, b, ab);
  (void)fr::inverseInto(ab, inv);
  const float P[3] = {1.0f, 0.0f, 0.0f};
  float p[3], v[3];
  (void)fr::transformPointInto(inv, P, p);
  (void)fr::rotateVectorInto(inv, P, v);
  static_assert(sizeof(fr::Transform<float>) == 28, "flat POD");
  return p[0] + v[1] + (fr::ok(fr::Status::SUCCESS) ? 1.0f : 0.0f);
}
