# ==============================================================================
# pico-gcc.cmake - Bare-metal ARM Cortex-M0+ toolchain
# ==============================================================================
#
# Target: RP2040 (Dual Cortex-M0+, no FPU)
# Compiler: arm-none-eabi-gcc (from ARM GNU Toolchain)
#
# Usage:
#   cmake --preset pico-baremetal
#   cmake --build --preset pico-baremetal
#
# The Pico SDK (when available in the Docker container) extends this
# toolchain with boot stage 2, linker scripts, and UF2 generation.
# ==============================================================================

# Bare-metal ARM (no OS)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ASM compile rule - must include DEFINES and INCLUDES for Pico SDK assembly
# files that use #include directives (boot_stage2, irq_handler_chain)
set(CMAKE_ASM_COMPILE_OBJECT
    "<CMAKE_ASM_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE>"
)
set(CMAKE_INCLUDE_FLAG_ASM "-I")

# ------------------------------------------------------------------------------
# Compilers
# ------------------------------------------------------------------------------

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

# Binary utilities
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_OBJDUMP arm-none-eabi-objdump)
set(CMAKE_SIZE arm-none-eabi-size)

# ------------------------------------------------------------------------------
# CPU Flags (Cortex-M0+, no FPU)
# ------------------------------------------------------------------------------

set(CPU_FLAGS "-mcpu=cortex-m0plus -mthumb")

# ------------------------------------------------------------------------------
# C++ Standard
# ------------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD
    20
    CACHE STRING "C++20 for Pico" FORCE
)

# ------------------------------------------------------------------------------
# Compiler Flags
# ------------------------------------------------------------------------------

# Warnings (strict but practical)
set(COMMON_WARN "-Wall -Wextra")

# Optimization for size and dead code elimination
set(OPT_FLAGS "-ffunction-sections -fdata-sections")

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

set(CMAKE_C_FLAGS_DEBUG_INIT "-Og -g3 -gdwarf-4")
set(CMAKE_CXX_FLAGS_DEBUG_INIT "-Og -g3 -gdwarf-4")
set(CMAKE_C_FLAGS_RELEASE_INIT "-Os -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE_INIT "-Os -DNDEBUG")
set(CMAKE_C_FLAGS_RELWITHDEBINFO_INIT "-Os -g -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO_INIT "-Os -g -DNDEBUG")

# ------------------------------------------------------------------------------
# Linker Flags
# ------------------------------------------------------------------------------
# Intentionally empty: Pico SDK creates internal executable targets (boot_stage2)
# that break with --gc-sections. The SDK provides its own linker flags via
# pico_stdlib INTERFACE properties, and apex_add_firmware() adds per-target flags.

# ------------------------------------------------------------------------------
# Search Paths
# ------------------------------------------------------------------------------

# Don't search host paths for libraries
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Pico SDK path (if available)
if (DEFINED ENV{PICO_SDK_PATH})
  list(APPEND CMAKE_PREFIX_PATH "$ENV{PICO_SDK_PATH}")
endif ()
