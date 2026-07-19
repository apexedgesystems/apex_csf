/**
 * @file Probe.cpp
 * @brief Compile probe: instantiates the library's public surface. Built on
 *        every platform the lib.manifest declares — under each cross
 *        toolchain on MCU builds, at each declared posix_cpp dialect on
 *        hosted builds — so a regression against the support contract fails
 *        the build that owns the claim.
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
