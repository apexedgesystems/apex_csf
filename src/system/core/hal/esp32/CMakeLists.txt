# ==============================================================================
# Module: hal_esp32
#
# ESP32 HAL implementations.
# Requires ESP-IDF headers at compile time.
#
# Libraries:
#   hal_esp32 - ESP32 UART, NVS Flash, USB CDC, tick source implementations (header-only)
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Interface Library (header-only, requires ESP-IDF at link time)
# ------------------------------------------------------------------------------

apex_add_interface_library(
  NAME
  hal_esp32
  INC
  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  DEPS_INTERFACE
  hal_interface
  BAREMETAL
)

# Additional include for src/ path resolution
target_include_directories(hal_esp32 INTERFACE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(hal_esp32)

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

# Unit tests (mock mode, host-side)
if (NOT APEX_PLATFORM_BAREMETAL AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/utst/CMakeLists.txt")
  add_subdirectory(utst)
endif ()
