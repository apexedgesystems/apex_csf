# ==============================================================================
# stm32-gcc.cmake - Bare-metal ARM Cortex-M toolchain for STM32 boards
# ==============================================================================
#
# Compiler: arm-none-eabi-gcc (from ARM GNU Toolchain)
#
# The board selects the CPU core and FPU flags through APEX_STM32_BOARD
# (a cache variable, passed like any other -D option):
#   nucleo_l476rg  STM32L476RG, Cortex-M4F  (default)
#   nucleo_f767zi  STM32F767ZI, Cortex-M7 with double-precision FPU
#   nucleo_f446re  STM32F446RE, Cortex-M4F
#
# Usage:
#   cmake --preset mcu-stm32-relwithdebinfo
#   cmake --preset mcu-stm32-relwithdebinfo -DAPEX_STM32_BOARD=nucleo_f767zi
#   cmake --build --preset mcu-stm32-relwithdebinfo
#
# A build directory is configured for one board and remembers it in the
# cache, so later configures without the option keep building that board.
# The CPU flags are copied into the cache on the first configure, so an
# explicit configure for a different board is refused instead of silently
# keeping the old flags.
# ==============================================================================

# Bare-metal ARM (no OS)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# HAL platform (root CMakeLists adds APEX_PLATFORM_<X>); -D override still wins.
set(APEX_HAL_PLATFORM
    "stm32"
    CACHE STRING "HAL platform selected by this toolchain"
)

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
# Board -> CPU / FPU Flags
# ------------------------------------------------------------------------------

set(APEX_STM32_BOARD
    "nucleo_l476rg"
    CACHE STRING "STM32 board: nucleo_l476rg | nucleo_f767zi | nucleo_f446re"
)

if (APEX_STM32_BOARD STREQUAL "nucleo_l476rg" OR APEX_STM32_BOARD STREQUAL "nucleo_f446re")
  set(CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16")
elseif (APEX_STM32_BOARD STREQUAL "nucleo_f767zi")
  set(CPU_FLAGS "-mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16")
else ()
  message(
    FATAL_ERROR
      "APEX_STM32_BOARD='${APEX_STM32_BOARD}' is not a known board (nucleo_l476rg | nucleo_f767zi | nucleo_f446re)"
  )
endif ()

# The CPU flags reach the compiler through CMAKE_<LANG>_FLAGS_INIT, which
# CMake copies into the cache once. Refuse a board change on an existing
# build directory so the binary never carries another board's flags.
if (DEFINED APEX_STM32_BOARD_CONFIGURED AND NOT APEX_STM32_BOARD_CONFIGURED STREQUAL
                                            APEX_STM32_BOARD
)
  message(
    FATAL_ERROR
      "This build directory was configured for APEX_STM32_BOARD=${APEX_STM32_BOARD_CONFIGURED}; "
      "remove build/mcu-stm32-* before configuring for ${APEX_STM32_BOARD}"
  )
endif ()
set(APEX_STM32_BOARD_CONFIGURED
    "${APEX_STM32_BOARD}"
    CACHE INTERNAL "Board the build directory was first configured for"
)

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

# STM32Cube paths (if available)
if (DEFINED ENV{STM32CUBE_L4_PATH})
  list(APPEND CMAKE_PREFIX_PATH "$ENV{STM32CUBE_L4_PATH}")
endif ()
if (DEFINED ENV{STM32CUBE_F7_PATH})
  list(APPEND CMAKE_PREFIX_PATH "$ENV{STM32CUBE_F7_PATH}")
endif ()
