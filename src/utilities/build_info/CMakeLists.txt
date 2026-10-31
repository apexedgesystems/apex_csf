# ==============================================================================
# Module: utilities_build_info
#
# Build identity: git describe, commit, branch, tree state, build type,
# compiler and target system, generated into one translation unit on every
# build (apex/BuildInfo) and rewritten only when it changes. Executives log the
# summary at boot so a run log names the source state behind the binaries in
# its bank.
# ==============================================================================

include(apex/All)
apex_module(utilities_build_info)

# Hosted only: the identity is read by executives, and firmware trees have no
# banner to carry it yet.
if (APEX_PLATFORM_BAREMETAL)
  return()
endif ()

set(BUILD_INFO_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/BuildInfo.cpp")

# ------------------------------------------------------------------------------
# Refresh
# ------------------------------------------------------------------------------

apex_add_build_info(
  NAME ${LIB_NAME}_refresh TEMPLATE "${SRC_DIR}/BuildInfo.cpp.in" OUTPUT "${BUILD_INFO_SOURCE}"
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
  "${BUILD_INFO_SOURCE}"
  DEPS_PUBLIC
  utilities_compatibility
  NO_PCH
)
add_dependencies(${LIB_NAME} ${LIB_NAME}_refresh)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs()
