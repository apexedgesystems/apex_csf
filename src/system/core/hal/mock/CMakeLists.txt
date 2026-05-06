# ==============================================================================
# Module: hal_mock
#
# Software-only HAL implementations for deterministic testing. Edges,
# events, and other inputs are injected by the test harness rather than
# read from hardware, so unit tests are reproducible and platform-free.
#
# Libraries:
#   hal_mock - Mock HAL implementations (INTERFACE, header-only)
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Interface Library (header-only)
# ------------------------------------------------------------------------------

apex_add_interface_library(
  NAME
  hal_mock
  INC
  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  DEPS_INTERFACE
  hal_interface
  BAREMETAL
)

# Additional include for src/ path resolution
target_include_directories(hal_mock INTERFACE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(hal_mock)

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

# Unit tests (host-side; mocks themselves require no platform)
if (NOT APEX_PLATFORM_BAREMETAL AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/utst/CMakeLists.txt")
  add_subdirectory(utst)
endif ()
