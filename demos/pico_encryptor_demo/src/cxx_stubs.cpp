/**
 * @file cxx_stubs.cpp
 * @brief Minimal C++ runtime stubs for bare-metal RP2040.
 *
 * Provides required symbols when linking C++ code with -fno-exceptions
 * and -fno-rtti. These are needed for virtual functions and static
 * destructors.
 *
 * The Pico SDK provides newlib stubs, __dso_handle, and operator
 * delete through pico_cxx_options. We only provide C++ ABI symbols
 * that the SDK does not supply.
 */

#include <stdint.h>

extern "C" {

/* Called when a pure virtual function is invoked (should never happen) */
void __cxa_pure_virtual() {
  while (1) {
  }
}

/* Guard for one-time static initialization (not needed with -fno-threadsafe-statics) */
int __cxa_guard_acquire(uint64_t* guard) {
  if (*guard)
    return 0;
  return 1;
}

void __cxa_guard_release(uint64_t* guard) { *guard = 1; }

void __cxa_guard_abort(uint64_t*) {}

/* Static destructor registration (we don't run destructors at exit) */
int __cxa_atexit(void (*)(void*), void*, void*) { return 0; }

} // extern "C"
