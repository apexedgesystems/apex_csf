# ==============================================================================
# system_core_action - Runtime action orchestration component
#
# Wraps ActionInterface (from system_core_data) as a CoreComponentBase.
# Provides the executive with a managed lifecycle for the action engine:
# watchpoint evaluation, group logic, event dispatch, sequence stepping,
# and action processing each scheduler cycle.
#
# Dependents: executive
# ==============================================================================

include(apex/All)
apex_module(system_core_action)

# ------------------------------------------------------------------------------
# Sources (2 files)
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/ActionComponent.cpp" "${SRC_DIR}/ActionComponentStatus.cpp")

# ------------------------------------------------------------------------------
# Library
# ------------------------------------------------------------------------------

apex_add_library(
  NAME
  ${LIB_NAME}
  TYPE
  SHARED
  INC
  "${INC_DIR}"
  SRC
  ${CPP_SOURCES}
  DEPS_PUBLIC
  system_core_data
  system_core_system_component
  utilities_concurrency
  utilities_helpers
  utilities_time
  fmt::fmt
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst)
