# ==============================================================================
# system_core_system_component - Full-featured component base classes
#
# Provides SystemComponentBase and derived classes for ApexExecutive.
# Heavy dependencies allowed (std::filesystem, std::shared_ptr, etc.)
#
# Dependents: executive, scheduler, interface, registry, filesystem, models
# ==============================================================================

include(apex/All)
apex_module(system_core_system_component)

# ------------------------------------------------------------------------------
# Sources (2 files)
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/SystemComponentBase.cpp" "${SRC_DIR}/PluginLoader.cpp")

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
  system_component_base
  system_component_core
  utilities_helpers
  utilities_time
  system_core_logs
  system_core_schedulable
  fmt::fmt
  DEPS_PRIVATE
  system_core_protocols_aproto
  utilities_concurrency
  ${CMAKE_DL_LIBS}
)

# ------------------------------------------------------------------------------
# Data Definition (struct dictionary for C2)
# ------------------------------------------------------------------------------

apex_add_data_manifest()

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst)
