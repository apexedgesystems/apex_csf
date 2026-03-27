# ==============================================================================
# Module: hal_c2000
#
# TI C2000 F28004x HAL implementations (C++03 compatible).
# Uses driverlib API on-device; mock mode for host-side testing.
#
# Libraries:
#   hal_c2000 - C2000 UART and tick source implementations (header-only)
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(
  NAME
  hal_c2000
  INC
  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  DEPS_INTERFACE
  utilities_compatibility
  BAREMETAL
)

# Additional include for src/ path resolution
target_include_directories(hal_c2000 INTERFACE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(hal_c2000)

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

# Unit tests (mock mode, host-side)
if (NOT APEX_PLATFORM_BAREMETAL AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/utst/CMakeLists.txt")
  add_subdirectory(utst)
endif ()
