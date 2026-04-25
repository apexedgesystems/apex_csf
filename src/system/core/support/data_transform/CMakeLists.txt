# ==============================================================================
# Module: system_core_support_data_transform
#
# Support component for runtime data mutation via byte mask proxies.
# Provides command-driven fault injection, value overrides, and safing.
# ==============================================================================

include(apex/All)
apex_module(system_core_support_data_transform)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/DataTransform.cpp")

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
  utilities_data_proxy
  DEPS_PRIVATE
  utilities_helpers
  fmt::fmt
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

if (EXISTS "${UTST_DIR}/CMakeLists.txt")
  add_subdirectory(utst)
endif ()

if (EXISTS "${PTST_DIR}/CMakeLists.txt")
  add_subdirectory(ptst)
endif ()
