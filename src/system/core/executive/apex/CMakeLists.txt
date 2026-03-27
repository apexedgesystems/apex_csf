# ==============================================================================
# system_core_executive - Full-featured executive for ApexExecutive
#
# Provides ApexExecutive and related classes for resource-rich systems.
# Heavy dependencies allowed (std::filesystem, std::thread, etc.)
#
# Dependents: apps (ApexDemo, etc.)
# ==============================================================================

include(apex/All)
apex_module(system_core_executive)

# ------------------------------------------------------------------------------
# Sources (10 files)
# ------------------------------------------------------------------------------

set(CPP_SOURCES
    "${SRC_DIR}/ExecutiveStatus.cpp"
    "${SRC_DIR}/ApexExecutive.cpp"
    "${SRC_DIR}/ApexExecutive_CLI.cpp"
    "${SRC_DIR}/ApexExecutive_Clock.cpp"
    "${SRC_DIR}/ApexExecutive_Commands.cpp"
    "${SRC_DIR}/ApexExecutive_ExternalIO.cpp"
    "${SRC_DIR}/ApexExecutive_Shutdown.cpp"
    "${SRC_DIR}/ApexExecutive_Startup.cpp"
    "${SRC_DIR}/ApexExecutive_TaskExecution.cpp"
    "${SRC_DIR}/ApexExecutive_Watchdog.cpp"
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
  executive_base
  system_core_action
  system_core_registry
  system_core_interface
  fmt::fmt
  DEPS_PRIVATE
  system_core_scheduler
  system_core_filesystem
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(tst)
