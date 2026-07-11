/**
 * @file FloorProbe.cpp
 * @brief MCU-floor probe: instantiates the library's public surface; compiled
 *        at C++17 with -fno-exceptions -fno-rtti by apex_add_floor_check so a
 *        hosted-only regression fails every PR build, not the next firmware
 *        build.
 */

#include "src/utilities/concurrency/mcu/inc/Delegate.hpp"

using namespace apex::concurrency;

struct Ctx {
  float scale;
};
static float scaleFn(void* c, float x) noexcept { return x * static_cast<Ctx*>(c)->scale; }
static uint8_t tick(void*) noexcept { return 0; }
static void fire(void*, int) noexcept {}

float probe() {
  Ctx ctx{2.0f};
  Delegate<float, float> d = makeDelegate<float, float>(&scaleFn, &ctx);
  DelegateU8 u{&tick, nullptr};
  Delegate<void, int> v{&fire, nullptr};
  v(7);
  static_assert(sizeof(d) == 2 * sizeof(void*), "delegate must stay two pointers");
  return d(3.0f) + static_cast<float>(u()) + (v ? 1.0f : 0.0f);
}
