# ==============================================================================
# executive_mcu - Minimal executive for MCU targets
#
# Header-only library providing McuExecutive for resource-constrained systems.
# Single-threaded, no heap allocation in hot path.
#
# Components:
#   - ITickSource       - Abstract tick source interface
#   - FreeRunningSource - No-wait tick source for testing
#   - McuExecutive     - Minimal IExecutive implementation
# ==============================================================================

include(apex/All)
apex_module(executive_mcu)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(
  NAME
  ${LIB_NAME}
  INC
  "${INC_DIR}"
  DEPS_INTERFACE
  executive_base
  executive_core
  scheduler_lite
  system_component_base
  system_component_mcu
  BAREMETAL
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst)
