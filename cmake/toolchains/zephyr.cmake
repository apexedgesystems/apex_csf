# ==============================================================================
# Toolchain: Zephyr RTOS
# ==============================================================================
#
# Purpose:
#   - Configure CMake to use Zephyr SDK compilers
#   - Support multiple target architectures via ZEPHYR_BOARD
#   - Integrate with Zephyr's build system
#
# Usage:
#   # Set board before configuring
#   cmake -DZEPHYR_BOARD=nucleo_l476rg -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/zephyr.cmake ..
#
# Supported boards (examples):
#   - nucleo_l476rg (STM32L476)
#   - esp32 (Espressif ESP32)
#   - nrf52840dk_nrf52840 (Nordic nRF52840)
#   - qemu_cortex_m3 (QEMU emulation)
#
# ==============================================================================

# ------------------------------------------------------------------------------
# Zephyr SDK location
# ------------------------------------------------------------------------------
# Default install location, override with ZEPHYR_SDK_INSTALL_DIR
set(ZEPHYR_SDK_INSTALL_DIR
    "/opt/zephyr-sdk"
    CACHE PATH "Zephyr SDK installation directory"
)

if (NOT EXISTS "${ZEPHYR_SDK_INSTALL_DIR}/sdk_version")
  message(FATAL_ERROR "Zephyr SDK not found at ${ZEPHYR_SDK_INSTALL_DIR}\n"
                      "Install with: west sdk install --install-dir /opt/zephyr-sdk"
  )
endif ()

# Read SDK version
file(READ "${ZEPHYR_SDK_INSTALL_DIR}/sdk_version" ZEPHYR_SDK_VERSION)
string(STRIP "${ZEPHYR_SDK_VERSION}" ZEPHYR_SDK_VERSION)

# ------------------------------------------------------------------------------
# Target board
# ------------------------------------------------------------------------------
set(ZEPHYR_BOARD
    ""
    CACHE STRING "Zephyr target board (e.g., nucleo_l476rg, esp32)"
)

if (NOT ZEPHYR_BOARD)
  message(
    FATAL_ERROR "ZEPHYR_BOARD not set. Specify target board (e.g., -DZEPHYR_BOARD=nucleo_l476rg)"
  )
endif ()

# ------------------------------------------------------------------------------
# Architecture detection from board
# ------------------------------------------------------------------------------
# Map common boards to architectures for compiler selection
set(_BOARD_ARCH_MAP
    "nucleo_:arm"
    "stm32:arm"
    "nrf:arm"
    "esp32:xtensa"
    "qemu_cortex:arm"
    "qemu_x86:x86"
    "qemu_riscv:riscv"
    "native_posix:native"
    "rpi_pico:arm"
)

set(ZEPHYR_ARCH "arm") # Default to ARM
foreach (_mapping ${_BOARD_ARCH_MAP})
  string(REPLACE ":" ";" _parts "${_mapping}")
  list(GET _parts 0 _prefix)
  list(GET _parts 1 _arch)
  if (ZEPHYR_BOARD MATCHES "^${_prefix}")
    set(ZEPHYR_ARCH "${_arch}")
    break()
  endif ()
endforeach ()

# ------------------------------------------------------------------------------
# Compiler selection based on architecture
# ------------------------------------------------------------------------------
set(CMAKE_SYSTEM_NAME Generic)

if (ZEPHYR_ARCH STREQUAL "arm")
  set(CMAKE_SYSTEM_PROCESSOR arm)
  set(CROSS_COMPILE "${ZEPHYR_SDK_INSTALL_DIR}/arm-zephyr-eabi/bin/arm-zephyr-eabi-")
  set(CMAKE_C_COMPILER "${CROSS_COMPILE}gcc")
  set(CMAKE_CXX_COMPILER "${CROSS_COMPILE}g++")
  set(CMAKE_ASM_COMPILER "${CROSS_COMPILE}gcc")

elseif (ZEPHYR_ARCH STREQUAL "riscv")
  set(CMAKE_SYSTEM_PROCESSOR riscv)
  set(CROSS_COMPILE "${ZEPHYR_SDK_INSTALL_DIR}/riscv64-zephyr-elf/bin/riscv64-zephyr-elf-")
  set(CMAKE_C_COMPILER "${CROSS_COMPILE}gcc")
  set(CMAKE_CXX_COMPILER "${CROSS_COMPILE}g++")
  set(CMAKE_ASM_COMPILER "${CROSS_COMPILE}gcc")

elseif (ZEPHYR_ARCH STREQUAL "xtensa")
  set(CMAKE_SYSTEM_PROCESSOR xtensa)
  # Xtensa toolchain varies by SoC, ESP32 uses esp32 variant
  if (ZEPHYR_BOARD MATCHES "esp32")
    set(CROSS_COMPILE
        "${ZEPHYR_SDK_INSTALL_DIR}/xtensa-espressif_esp32_zephyr-elf/bin/xtensa-espressif_esp32_zephyr-elf-"
    )
  else ()
    set(CROSS_COMPILE "${ZEPHYR_SDK_INSTALL_DIR}/xtensa-zephyr-elf/bin/xtensa-zephyr-elf-")
  endif ()
  set(CMAKE_C_COMPILER "${CROSS_COMPILE}gcc")
  set(CMAKE_CXX_COMPILER "${CROSS_COMPILE}g++")
  set(CMAKE_ASM_COMPILER "${CROSS_COMPILE}gcc")

elseif (ZEPHYR_ARCH STREQUAL "x86")
  set(CMAKE_SYSTEM_PROCESSOR x86)
  set(CROSS_COMPILE "${ZEPHYR_SDK_INSTALL_DIR}/x86_64-zephyr-elf/bin/x86_64-zephyr-elf-")
  set(CMAKE_C_COMPILER "${CROSS_COMPILE}gcc")
  set(CMAKE_CXX_COMPILER "${CROSS_COMPILE}g++")
  set(CMAKE_ASM_COMPILER "${CROSS_COMPILE}gcc")

elseif (ZEPHYR_ARCH STREQUAL "native")
  # Native POSIX - use host compiler
  set(CMAKE_SYSTEM_NAME Linux)
  set(CMAKE_SYSTEM_PROCESSOR ${CMAKE_HOST_SYSTEM_PROCESSOR})
endif ()

# ------------------------------------------------------------------------------
# Zephyr environment
# ------------------------------------------------------------------------------
set(ZEPHYR_TOOLCHAIN_VARIANT
    zephyr
    CACHE STRING ""
)
set(ENV{ZEPHYR_SDK_INSTALL_DIR} "${ZEPHYR_SDK_INSTALL_DIR}")
set(ENV{ZEPHYR_TOOLCHAIN_VARIANT} "zephyr")

# ------------------------------------------------------------------------------
# Bare-metal settings
# ------------------------------------------------------------------------------
if (NOT ZEPHYR_ARCH STREQUAL "native")
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
  set(BUILD_SHARED_LIBS
      OFF
      CACHE BOOL "" FORCE
  )
endif ()

# ------------------------------------------------------------------------------
# Debug output
# ------------------------------------------------------------------------------
set(APEX_TOOLCHAIN_VERBOSE
    OFF
    CACHE BOOL "Print toolchain debug lines"
)
if (APEX_TOOLCHAIN_VERBOSE)
  message(STATUS "[zephyr] SDK=${ZEPHYR_SDK_INSTALL_DIR} (v${ZEPHYR_SDK_VERSION})")
  message(STATUS "[zephyr] BOARD=${ZEPHYR_BOARD} ARCH=${ZEPHYR_ARCH}")
  message(STATUS "[zephyr] CC=${CMAKE_C_COMPILER}")
endif ()
