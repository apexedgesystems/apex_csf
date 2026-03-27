# ==============================================================================
# Module: system_core_data
#
# Static-sized data infrastructure for models and components.
#
# Provides:
#   - DataCategory enum (StaticParam, TunableParam, State, Input, Output)
#   - FaultInjectionProxy (static-sized mask queue)
#   - EndiannessProxy (byte-swap support)
#   - MasterDataProxy (transformation orchestration)
#   - ModelData<T, Category> typed container with access control
#
# Design: All RT-safe, no dynamic allocation, trivially copyable types only.
#
# Libraries:
#   system_core_data - Header-only data infrastructure library
# ==============================================================================

include(apex/All)
apex_module(system_core_data)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(
  NAME
  ${LIB_NAME}
  INC
  "${INC_DIR}"
  DEPS_INTERFACE
  utilities_compatibility
  utilities_time
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

if (EXISTS "${TST_DIR}/CMakeLists.txt")
  add_subdirectory(tst)
endif ()

if (EXISTS "${UTST_DIR}/CMakeLists.txt")
  add_subdirectory(utst)
endif ()

if (EXISTS "${PTST_DIR}/CMakeLists.txt")
  add_subdirectory(ptst)
endif ()
