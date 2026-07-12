/**
 * @file cxx_stubs.cpp
 * @brief Minimal C++ runtime stubs for bare-metal AVR.
 *
 * Provides required symbols when using avr-g++ with virtual functions,
 * static initialization, and delete operators. Unlike the STM32 version,
 * we do NOT override _init/_fini since avr-libc provides them via its
 * crt startup code.
 *
 * Uses weak linkage to avoid conflicts if avr-libc or avr-libstdc++
 * already provide these symbols.
 */

#include <stddef.h>
#include <stdint.h>

extern "C" {

/* Called when a pure virtual function is invoked (should never happen) */
__attribute__((weak)) void __cxa_pure_virtual() {
  while (1) {
  }
}

/* Static destructor registration (we don't run destructors at exit) */
__attribute__((weak)) int __cxa_atexit(void (*)(void*), void*, void*) { return 0; }

} // extern "C"

/* Placement delete (required when virtual destructor is called) */
__attribute__((weak)) void operator delete(void*, size_t) noexcept {}

__attribute__((weak)) void operator delete(void*) noexcept {}
