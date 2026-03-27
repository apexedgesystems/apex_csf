# ==============================================================================
# esp32-gcc.cmake - Bare-metal Xtensa LX7 toolchain for ESP32-S3
# ==============================================================================
#
# Target: ESP32-S3 (Dual Xtensa LX7, single-precision FPU)
# Compiler: xtensa-esp-elf-gcc (from ESP-IDF toolchain)
#
# Usage:
#   cmake --preset esp32-baremetal
#   cmake --build --preset esp32-baremetal
#
# The ESP-IDF framework (when available in the Docker container) extends
# this toolchain with component libraries, linker scripts, sdkconfig, and
# binary generation via idf_build_process()/idf_build_executable().
# ==============================================================================

# Bare-metal (no OS -- FreeRTOS is linked as a library by ESP-IDF)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR xtensa)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ------------------------------------------------------------------------------
# Tool Paths
# ------------------------------------------------------------------------------
# ESP-IDF tools are installed to IDF_TOOLS_PATH (default /opt/espressif).
# Resolve the compiler prefix from environment or default install location.

if (DEFINED ENV{IDF_TOOLS_PATH})
  set(_IDF_TOOLS "$ENV{IDF_TOOLS_PATH}")
else ()
  set(_IDF_TOOLS "/opt/espressif")
endif ()

# Find toolchain bin directory under the tools directory
file(GLOB _xtensa_dirs "${_IDF_TOOLS}/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin")
list(SORT _xtensa_dirs ORDER DESCENDING)
if (_xtensa_dirs)
  list(GET _xtensa_dirs 0 _xtensa_bin)
else ()
  set(_xtensa_bin "${_IDF_TOOLS}/tools/xtensa-esp-elf/xtensa-esp-elf/bin")
endif ()

# ------------------------------------------------------------------------------
# Compilers
# ------------------------------------------------------------------------------
# Use target-specific binaries (xtensa-esp32s3-elf-*) which auto-load the
# ESP32-S3 dynconfig for correct instruction set support (rer, wer, etc.).
# The generic xtensa-esp-elf-* binaries lack target configuration.

set(CMAKE_C_COMPILER "${_xtensa_bin}/xtensa-esp32s3-elf-gcc")
set(CMAKE_CXX_COMPILER "${_xtensa_bin}/xtensa-esp32s3-elf-g++")
set(CMAKE_ASM_COMPILER "${_xtensa_bin}/xtensa-esp32s3-elf-gcc")

# Binary utilities
set(CMAKE_OBJCOPY "${_xtensa_bin}/xtensa-esp32s3-elf-objcopy")
set(CMAKE_OBJDUMP "${_xtensa_bin}/xtensa-esp32s3-elf-objdump")
set(CMAKE_SIZE "${_xtensa_bin}/xtensa-esp32s3-elf-size")

# ------------------------------------------------------------------------------
# C++ Standard
# ------------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD
    23
    CACHE STRING "C++23 for ESP32 (GCC 14.2)" FORCE
)

# ------------------------------------------------------------------------------
# Compiler Flags
# ------------------------------------------------------------------------------

# Warnings (strict but practical)
set(COMMON_WARN "-Wall -Wextra")

# Optimization for size, dead code elimination, and long-range calls.
# -mlongcalls is required on Xtensa: the call8 instruction has a ~512 KB
# range limit.  Without it the linker emits "dangerous relocation: call8:
# call target out of range" once the binary exceeds that threshold.
set(OPT_FLAGS "-mlongcalls -ffunction-sections -fdata-sections")

# C-specific flags
set(CMAKE_C_FLAGS_INIT "${COMMON_WARN} ${OPT_FLAGS}")

# C++ specific flags (no exceptions/RTTI for bare-metal)
set(CMAKE_CXX_FLAGS_INIT
    "${COMMON_WARN} ${OPT_FLAGS} -fno-exceptions -fno-rtti -fno-threadsafe-statics"
)

# Assembly flags
set(CMAKE_ASM_FLAGS_INIT "")

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
# Search Paths
# ------------------------------------------------------------------------------

# Don't search host paths for libraries
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
