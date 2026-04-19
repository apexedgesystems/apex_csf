# ==============================================================================
# system_core_registry - Full-featured registry implementation
#
# Unified registry for components, tasks, and data in an Apex application.
#
# Components:
#   - ApexRegistry     - Central database for runtime component/task/data tracking
#   - RegistryData     - Entry structures (ComponentEntry, TaskEntry, DataEntry)
#   - RegistryExport   - RDAT binary export format
#   - RegistryStatus   - Typed status codes for registry operations
#
# Dependencies:
#   system_core_system_component - Base class for system components
# ==============================================================================

include(apex/All)
apex_module(system_core_registry)

# ------------------------------------------------------------------------------
# Sources (2 files)
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/RegistryStatus.cpp" "${SRC_DIR}/ApexRegistry.cpp")

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
