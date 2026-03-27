# ==============================================================================
# system_core_scheduler - Full-featured scheduler implementations
#
# Provides SchedulerBase and derived classes for ApexExecutive.
# Heavy dependencies allowed (std::thread, std::mutex, etc.)
#
# Dependents: executive, models
# ==============================================================================

include(apex/All)
apex_module(system_core_scheduler)

# ------------------------------------------------------------------------------
# Sources (4 files)
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/SchedulerStatus.cpp" "${SRC_DIR}/SchedulerBase.cpp"
                "${SRC_DIR}/SchedulerSingleThread.cpp" "${SRC_DIR}/SchedulerMultiThread.cpp"
)

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
  scheduler_base
  system_core_schedulable
  system_core_system_component
  fmt::fmt
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst)
