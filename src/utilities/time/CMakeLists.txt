# ==============================================================================
# Module: utilities_time
#
# Time standard definitions, system clock providers, and conversion utilities
# for real-time and embedded systems. Header-only.
#
# The time-domain types and conversions (TimeBase, TimeConvert) ship from the
# mcu/ subset; this hosted layer adds the POSIX clock provider set
# (SystemClocks: monotonic, UTC, TAI, GPS, MET).
# ==============================================================================

include(apex/All)

add_subdirectory(mcu)

# The POSIX provider set (SystemClocks) below never targets bare-metal; the
# mcu subset above is the freestanding core.
if (APEX_PLATFORM_BAREMETAL)
  return()
endif ()

apex_module(utilities_time)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(
  NAME
  ${LIB_NAME}
  INC
  "${INC_DIR}"
  DEPS_PUBLIC
  utilities_concurrency
  utilities_time_mcu
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs()
