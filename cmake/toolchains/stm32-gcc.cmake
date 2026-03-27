# ==============================================================================
# stm32-gcc.cmake - Bare-metal ARM Cortex-M4 toolchain
# ==============================================================================
#
# Target: STM32L4xx (Cortex-M4 with FPU)
# Compiler: arm-none-eabi-gcc (from ARM GNU Toolchain)
#
# Usage:
#   cmake --preset stm32-baremetal
#   cmake --build --preset stm32-baremetal
# ==============================================================================

# Bare-metal ARM (no OS)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Enable ASM language for startup files
set(CMAKE_ASM_COMPILE_OBJECT "<CMAKE_ASM_COMPILER> <FLAGS> -c <SOURCE> -o <OBJECT>")

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
# CPU / FPU Flags (Cortex-M4 with single-precision FPU)
# ------------------------------------------------------------------------------

set(CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16")

# ------------------------------------------------------------------------------
# C++ Standard (arm-none-eabi-g++ 10.3 supports up to C++20)
# ------------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD
    20
    CACHE STRING "C++20 for STM32" FORCE
)

# ------------------------------------------------------------------------------
# Compiler Flags
# ------------------------------------------------------------------------------

# Warnings (strict but practical)
set(COMMON_WARN "-Wall -Wextra -Wno-psabi")

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
# Linker Flags (base - linker script added per-target)
# ------------------------------------------------------------------------------

# Note: Linker script is specified per-target via apex_add_firmware()
# These are base flags applied to all executables
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--gc-sections -Wl,--print-memory-usage")

# ------------------------------------------------------------------------------
# Search Paths
# ------------------------------------------------------------------------------

# Don't search host paths for libraries
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# STM32Cube path (if available)
if (DEFINED ENV{STM32CUBE_L4_PATH})
  list(APPEND CMAKE_PREFIX_PATH "$ENV{STM32CUBE_L4_PATH}")
endif ()
