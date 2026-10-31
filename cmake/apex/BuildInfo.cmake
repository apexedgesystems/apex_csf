# ==============================================================================
# BuildInfo.cmake - Build identity
# ------------------------------------------------------------------------------
# Generates a translation unit carrying the source state and configuration a
# build came from (git describe, commit, branch, tree state, build type,
# compiler, target system) and keeps it current on every build.
#
# Two roles in one file:
#   - Included through apex/All: defines apex_add_build_info(), which a library
#     calls to get a refresh target for its generated source.
#   - Run in script mode (cmake -P) by that target: reads git, fills the
#     template, and rewrites the output only when its content changed, so an
#     unchanged identity recompiles and relinks nothing.
#
# Usage:
#   apex_add_build_info(
#     NAME     <refresh target>
#     TEMPLATE <BuildInfo.cpp.in>
#     OUTPUT   <generated BuildInfo.cpp>
#   )
#
# The identity carries no build time: it is a function of source state and
# configuration alone, so two builds of one tree agree. Git-derived fields
# read "unknown" when git or the repository metadata is absent (a source
# archive, an image build context that omits .git).
# ==============================================================================

# ------------------------------------------------------------------------------
# Module role
# ------------------------------------------------------------------------------
if (NOT CMAKE_SCRIPT_MODE_FILE)
  include_guard(GLOBAL)

  function (apex_add_build_info)
    cmake_parse_arguments(BI "" "NAME;TEMPLATE;OUTPUT" "" ${ARGN})
    apex_require(BI_NAME BI_TEMPLATE BI_OUTPUT)

    find_package(Git QUIET)
    set(_git "")
    if (Git_FOUND)
      set(_git "${GIT_EXECUTABLE}")
    endif ()

    set_source_files_properties("${BI_OUTPUT}" PROPERTIES GENERATED TRUE)

    # A target with no inputs is always out of date, so the generator runs on
    # every build that needs the library; it leaves the output untouched when
    # the identity is unchanged.
    add_custom_target(
      ${BI_NAME}
      COMMAND
        ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DTEMPLATE=${BI_TEMPLATE}
        -DOUTPUT=${BI_OUTPUT} -DPROJECT_NAME=${CMAKE_PROJECT_NAME}
        -DPROJECT_VERSION=${CMAKE_PROJECT_VERSION} -DBUILD_TYPE=$<CONFIG>
        -DCOMPILER=${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}
        -DTARGET_SYSTEM=${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR} -DGIT_EXECUTABLE=${_git} -P
        ${CMAKE_CURRENT_FUNCTION_LIST_FILE}
      BYPRODUCTS "${BI_OUTPUT}"
      COMMENT "Refreshing the build identity"
      VERBATIM
    )
  endfunction ()

  return()
endif ()

# ------------------------------------------------------------------------------
# Script role: the generator
# ------------------------------------------------------------------------------
# Inputs (-D): SOURCE_DIR, TEMPLATE, OUTPUT, PROJECT_NAME, PROJECT_VERSION,
# COMPILER, TARGET_SYSTEM, BUILD_TYPE (may be empty), GIT_EXECUTABLE (empty
# when git is unavailable).
cmake_minimum_required(VERSION 3.24)

foreach (
  _required
  SOURCE_DIR
  TEMPLATE
  OUTPUT
  PROJECT_NAME
  PROJECT_VERSION
  COMPILER
  TARGET_SYSTEM
)
  if (NOT DEFINED ${_required})
    message(FATAL_ERROR "BuildInfo: ${_required} is required")
  endif ()
endforeach ()

if (NOT DEFINED BUILD_TYPE OR BUILD_TYPE STREQUAL "")
  set(BUILD_TYPE "unspecified")
endif ()

set(_describe "unknown")
set(_commit "unknown")
set(_branch "unknown")
set(_tree "unknown")

# Run git in the source tree; on any failure the caller keeps its default.
function (_bi_git _out)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" ${ARGN}
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if (_result EQUAL 0 AND NOT _output STREQUAL "")
    set(${_out}
        "${_output}"
        PARENT_SCOPE
    )
  endif ()
endfunction ()

if (GIT_EXECUTABLE AND EXISTS "${SOURCE_DIR}/.git")
  _bi_git(_describe describe --tags --always --dirty)
  _bi_git(_commit rev-parse --short=12 HEAD)
  _bi_git(_branch rev-parse --abbrev-ref HEAD)

  # Tree state: the diff of tracked files against HEAD, staged and unstaged
  # alike, which is the same set `git describe --dirty` inspects. Binary
  # hunks are emitted as text so the whole diff hashes.
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" diff HEAD --no-ext-diff --no-color --binary
    RESULT_VARIABLE _diff_result
    OUTPUT_VARIABLE _diff
    ERROR_QUIET
  )
  if (_diff_result EQUAL 0)
    if (_diff STREQUAL "")
      set(_tree "clean")
    else ()
      string(SHA256 _diff_hash "${_diff}")
      string(SUBSTRING "${_diff_hash}" 0 12 _tree)
    endif ()
  endif ()
endif ()

if (_tree STREQUAL "clean" OR _tree STREQUAL "unknown")
  set(_dirty "false")
else ()
  set(_dirty "true")
endif ()

# Every field lands inside a C++ string literal.
function (_bi_literal _var)
  string(REPLACE "\\" "\\\\" _value "${${_var}}")
  string(REPLACE "\"" "\\\"" _value "${_value}")
  set(${_var}
      "${_value}"
      PARENT_SCOPE
  )
endfunction ()

set(BI_PROJECT "${PROJECT_NAME}")
set(BI_VERSION "${PROJECT_VERSION}")
set(BI_DESCRIBE "${_describe}")
set(BI_COMMIT "${_commit}")
set(BI_BRANCH "${_branch}")
set(BI_TREE_STATE "${_tree}")
set(BI_DIRTY "${_dirty}")
set(BI_BUILD_TYPE "${BUILD_TYPE}")
set(BI_COMPILER "${COMPILER}")
set(BI_TARGET_SYSTEM "${TARGET_SYSTEM}")
set(BI_SUMMARY
    "${BI_PROJECT} ${BI_DESCRIBE} (${BI_BRANCH}) tree=${BI_TREE_STATE} ${BI_BUILD_TYPE} ${BI_COMPILER} ${BI_TARGET_SYSTEM}"
)
foreach (
  _field
  BI_PROJECT
  BI_VERSION
  BI_DESCRIBE
  BI_COMMIT
  BI_BRANCH
  BI_TREE_STATE
  BI_BUILD_TYPE
  BI_COMPILER
  BI_TARGET_SYSTEM
  BI_SUMMARY
)
  _bi_literal(${_field})
endforeach ()

# configure_file writes the output only when its content changes, which is
# what keeps an unchanged identity from recompiling anything.
configure_file("${TEMPLATE}" "${OUTPUT}" @ONLY)
