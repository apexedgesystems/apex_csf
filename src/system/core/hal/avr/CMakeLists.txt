# ==============================================================================
# Module: hal_avr
#
# AVR-specific HAL implementations (ATmega328P).
# Requires avr-libc headers at compile time.
#
# Libraries:
#   hal_avr - AVR UART, EEPROM, tick source implementations (header-only)
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Interface Library (header-only, requires avr-libc at link time)
# ------------------------------------------------------------------------------

apex_add_interface_library(
  NAME
  hal_avr
  INC
  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  DEPS_INTERFACE
  hal_interface
  BAREMETAL
)

# Additional include for src/ path resolution
target_include_directories(hal_avr INTERFACE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(hal_avr)

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

# Unit tests (mock mode, host-side)
if (NOT APEX_PLATFORM_BAREMETAL AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/utst/CMakeLists.txt")
  add_subdirectory(utst)
endif ()
