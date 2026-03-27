# ==============================================================================
# scheduler_base - Pure interface library (header-only)
#
# Provides the minimal IScheduler interface that all scheduler implementations
# must satisfy. No heavy dependencies - suitable for bare-metal.
#
# Dependents:
#   - scheduler (SchedulerBase, SchedulerMultiThread, SchedulerSingleThread)
#   - scheduler_lite (SchedulerLite)
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(
  NAME
  scheduler_base
  INC
  "${PROJECT_SOURCE_DIR}"
  DEPS_INTERFACE
  system_component_base
  BAREMETAL
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(scheduler_base)

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/utst/CMakeLists.txt")
  add_subdirectory(utst)
endif ()
