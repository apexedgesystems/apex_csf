# ==============================================================================
# scheduler_mcu - Minimal scheduler for MCU targets
#
# Header-only library providing SchedulerLite for resource-constrained systems.
# Static task table, no heap allocation in hot path.
#
# Components:
#   - SchedulerLite - Minimal IScheduler implementation
#   - LiteTaskEntry - Static task configuration
# ==============================================================================

include(apex/All)
apex_module(scheduler_mcu)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(
  NAME
  ${LIB_NAME}
  INC
  "${INC_DIR}"
  DEPS_INTERFACE
  scheduler_base
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

apex_add_test_subdirs(utst ptst)
