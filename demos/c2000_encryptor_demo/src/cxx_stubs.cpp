/**
 * @file cxx_stubs.cpp
 * @brief Minimal C++ runtime stubs for bare-metal C2000 (C++03).
 *
 * Provides required symbols when linking C++ code without full runtime support.
 * TI CGT provides rts2800_fpu32.lib for most C runtime but C++ virtual
 * functions and static init still need these stubs.
 */

#include <stdint.h>

extern "C" {

/* Required for static object destruction registration */
void* __dso_handle = 0;

/* Called when a pure virtual function is invoked (should never happen) */
void __cxa_pure_virtual() {
  for (;;) {
  }
}

/* Guard for one-time static initialization */
int __cxa_guard_acquire(uint32_t* guard) {
  if (*guard)
    return 0;
  return 1;
}

void __cxa_guard_release(uint32_t* guard) { *guard = 1; }

void __cxa_guard_abort(uint32_t*) {}

/* Static destructor registration (we don't run destructors at exit) */
int __cxa_atexit(void (*)(void*), void*, void*) { return 0; }

} /* extern "C" */
