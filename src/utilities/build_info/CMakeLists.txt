# ==============================================================================
# Module: utilities_build_info
#
# Build identity: git describe, commit, branch, tree state, build type,
# compiler and target system, generated into one translation unit on every
# build (apex/BuildInfo) and rewritten only when it changes. Executives log the
# summary at boot so a run log names the source state behind the binaries in
# its bank; firmware writes it as its first line on the UART. lib.manifest
# names the platforms: SHARED on hosted builds, STATIC under the MCU
# toolchains, where the constants link into the image.
# ==============================================================================

include(apex/All)
apex_module(utilities_build_info)

set(BUILD_INFO_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/BuildInfo.cpp")

# ------------------------------------------------------------------------------
# Library
# ------------------------------------------------------------------------------
# lib.manifest decides whether this platform builds the library; the refresh
# wiring below belongs to a library that exists.

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
if (NOT TARGET ${LIB_NAME})
  return()
endif ()

# ------------------------------------------------------------------------------
# Refresh
# ------------------------------------------------------------------------------

apex_add_build_info(
  NAME ${LIB_NAME}_refresh TEMPLATE "${SRC_DIR}/BuildInfo.cpp.in" OUTPUT "${BUILD_INFO_SOURCE}"
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
