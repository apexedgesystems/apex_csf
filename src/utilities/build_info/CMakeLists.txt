# ==============================================================================
# Module: utilities_build_info
#
# Build identity: git describe, commit, branch, tree state, build type,
# compiler and target system, generated into one translation unit on every
# build and rewritten only when it changes. Executives log the summary at boot
# so a run log names the source state behind the binaries in its bank.
# ==============================================================================

include(apex/All)
apex_module(utilities_build_info)

# Hosted only: the identity is read by executives, and firmware trees have no
# banner to carry it yet.
if (APEX_PLATFORM_BAREMETAL)
  return()
endif ()

find_package(Git QUIET)
if (NOT Git_FOUND)
  set(GIT_EXECUTABLE "")
endif ()

set(BUILD_INFO_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/BuildInfo.cpp")
set_source_files_properties("${BUILD_INFO_SOURCE}" PROPERTIES GENERATED TRUE)

# ------------------------------------------------------------------------------
# Refresh
# ------------------------------------------------------------------------------
# Runs on every build that needs the library (a target with no inputs is
# always out of date). The generator replaces the source only when the
# identity changed, so a build with nothing new recompiles and relinks nothing.
add_custom_target(
  ${LIB_NAME}_refresh
  COMMAND
    ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DTEMPLATE=${SRC_DIR}/BuildInfo.cpp.in
    -DOUTPUT=${BUILD_INFO_SOURCE} -DPROJECT_NAME=${CMAKE_PROJECT_NAME}
    -DPROJECT_VERSION=${CMAKE_PROJECT_VERSION} -DBUILD_TYPE=$<CONFIG>
    -DCOMPILER=${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}
    -DTARGET_SYSTEM=${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}
    -DGIT_EXECUTABLE=${GIT_EXECUTABLE} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateBuildInfo.cmake
  BYPRODUCTS "${BUILD_INFO_SOURCE}"
  COMMENT "Refreshing the build identity"
  VERBATIM
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
