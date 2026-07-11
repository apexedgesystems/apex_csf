/**
 * @file FloorProbe.cpp
 * @brief MCU-floor probe: instantiates the library's public surface; compiled
 *        at C++17 with -fno-exceptions -fno-rtti by apex_add_floor_check so a
 *        hosted-only regression fails every PR build, not the next firmware
 *        build.
 */

#include "src/utilities/math/quaternion/inc/QuatData.hpp"
#include "src/utilities/math/quaternion/inc/Quaternion.hpp"
#include "src/utilities/math/quaternion/inc/QuaternionIntegrator.hpp"

using namespace apex::math::quaternion;

float probe() {
  QuatData<float> q;
  const float W[3] = {0.1f, -0.2f, 0.3f};
  Quaternion<float> v = q.view();
  (void)QuaternionIntegrator<float>::stepExponential(v, W, 0.01f);
  float r = 0, p = 0, y = 0;
  (void)v.toEuler321Into(r, p, y);
  (void)v.setFromEuler321(r, p, y);
  float out[3];
  const float in[3] = {1.0f, 0.0f, 0.0f};
  (void)v.rotateVectorInto(in, out);
  return out[0] + p;
}
