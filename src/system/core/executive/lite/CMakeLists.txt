# ==============================================================================
# executive_lite - Minimal executive for MCU targets
#
# Header-only library providing LiteExecutive for resource-constrained systems.
# Single-threaded, no heap allocation in hot path.
#
# Components:
#   - ITickSource       - Abstract tick source interface
#   - FreeRunningSource - No-wait tick source for testing
#   - LiteExecutive     - Minimal IExecutive implementation
# ==============================================================================

include(apex/All)
apex_module(executive_lite)

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
  scheduler_lite
  system_component_base
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
