/**
 * @file cxx_stubs.cpp
 * @brief Minimal C++ runtime stubs for bare-metal embedded.
 *
 * Provides required symbols when linking C++ code without the standard library.
 * These are needed for virtual functions, static destructors, and delete.
 */

#include <stdint.h>

extern "C" {

/* Required for static object destruction registration */
void* __dso_handle = nullptr;

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

/* Required by newlib's __libc_init_array */
void _init() {}
void _fini() {}

} // extern "C"

/* Placement delete (required when destructor is called) */
void operator delete(void*, unsigned int) noexcept {}
void operator delete(void*) noexcept {}
