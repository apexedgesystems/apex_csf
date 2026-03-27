# ==============================================================================
# avr-gcc.cmake - Bare-metal AVR toolchain for ATmega328P
# ==============================================================================
#
# Target: ATmega328P (8-bit AVR @ 16 MHz, Arduino Uno R3)
# Compiler: avr-gcc (from Ubuntu gcc-avr package)
#
# Usage:
#   cmake --preset arduino-baremetal
#   cmake --build --preset arduino-baremetal
# ==============================================================================

# Bare-metal AVR (no OS)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR avr)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ------------------------------------------------------------------------------
# Compilers
# ------------------------------------------------------------------------------

set(CMAKE_C_COMPILER avr-gcc)
set(CMAKE_CXX_COMPILER avr-g++)
set(CMAKE_ASM_COMPILER avr-gcc)

# Binary utilities
set(CMAKE_OBJCOPY avr-objcopy)
set(CMAKE_OBJDUMP avr-objdump)
set(CMAKE_SIZE avr-size)

# ------------------------------------------------------------------------------
# CPU Flags (ATmega328P @ 16 MHz)
# ------------------------------------------------------------------------------

set(CPU_FLAGS "-mmcu=atmega328p -DF_CPU=16000000UL")

# ------------------------------------------------------------------------------
# C++ Standard (avr-g++ 7.3 maxes at C++17)
# ------------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD
    17
    CACHE STRING "C++17 for AVR" FORCE
)

# ------------------------------------------------------------------------------
# Compiler Flags
# ------------------------------------------------------------------------------

# Warnings (strict but practical)
set(COMMON_WARN "-Wall -Wextra")

# Optimize for size (critical on 32 KB flash) + dead code elimination
set(OPT_FLAGS "-ffunction-sections -fdata-sections -Os")

# C-specific flags
set(CMAKE_C_FLAGS_INIT "${CPU_FLAGS} ${COMMON_WARN} ${OPT_FLAGS}")

# C++ specific flags (no exceptions/RTTI for bare-metal)
set(CMAKE_CXX_FLAGS_INIT
    "${CPU_FLAGS} ${COMMON_WARN} ${OPT_FLAGS} -fno-exceptions -fno-rtti -fno-threadsafe-statics"
)

# Assembly flags
set(CMAKE_ASM_FLAGS_INIT "${CPU_FLAGS}")

# ------------------------------------------------------------------------------
# Build Type Flags
# ------------------------------------------------------------------------------

set(CMAKE_C_FLAGS_DEBUG_INIT "-Og -g3")
set(CMAKE_CXX_FLAGS_DEBUG_INIT "-Og -g3")
set(CMAKE_C_FLAGS_RELEASE_INIT "-Os -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE_INIT "-Os -DNDEBUG")
set(CMAKE_C_FLAGS_RELWITHDEBINFO_INIT "-Os -g -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO_INIT "-Os -g -DNDEBUG")

# ------------------------------------------------------------------------------
# Linker Flags
# ------------------------------------------------------------------------------

# avr-gcc uses -mmcu to select the correct linker script from avr-libc.
# No custom linker script needed -- avr-libc provides crt0 (startup),
# vector table, .data copy, .bss clear for ATmega328P.
set(CMAKE_EXE_LINKER_FLAGS_INIT "${CPU_FLAGS} -Wl,--gc-sections -Wl,--print-memory-usage")

# ------------------------------------------------------------------------------
# Search Paths
# ------------------------------------------------------------------------------

# Don't search host paths for libraries
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
