/**
 * @file FloorProbe.cpp
 * @brief MCU-floor probe: instantiates the library's public surface; compiled
 *        at C++17 with -fno-exceptions -fno-rtti by apex_add_floor_check so a
 *        hosted-only regression fails every PR build, not the next firmware
 *        build.
 */

#include "src/utilities/math/vecmat/inc/Mat3Ops.hpp"
#include "src/utilities/math/vecmat/inc/Rotations.hpp"
#include "src/utilities/math/vecmat/inc/Vec3Ops.hpp"

namespace vm = apex::math::vecmat;

float probe() {
  const float A[3] = {1.0f, 0.0f, 0.0f}, B[3] = {0.0f, 1.0f, 0.0f};
  float c[3], dcm[9], v[3], x[3];
  vm::cross(A, B, c);
  vm::dcmFromEuler321Into(0.1f, 0.2f, 0.3f, dcm);
  vm::multiplyVec(dcm, c, v);
  const float I[9] = {2.0f, 0.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f, 4.0f};
  (void)vm::solveInto(I, v, x);
  float r = 0, p = 0, y = 0;
  vm::euler321FromDcmInto(dcm, r, p, y);
  return x[0] + vm::dot(A, B) + r;
}
