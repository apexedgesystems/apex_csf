# ==============================================================================
# GenerateBuildInfo.cmake
# ------------------------------------------------------------------------------
# Writes the build identity translation unit from the source tree's git state
# and the configuration it is built with. Invoked in script mode on every
# build; the output file is replaced only when its content changes, so an
# unchanged identity recompiles and relinks nothing.
#
# Inputs (-D):
#   SOURCE_DIR      repository root
#   TEMPLATE        BuildInfo.cpp.in
#   OUTPUT          BuildInfo.cpp to write
#   PROJECT_NAME    CMake project name
#   PROJECT_VERSION CMake project version
#   BUILD_TYPE      configuration being built (may be empty)
#   COMPILER        "<id>-<version>"
#   TARGET_SYSTEM   "<system>/<processor>"
#   GIT_EXECUTABLE  path to git, or empty when git is unavailable
#
# Git-derived fields read "unknown" when git or the repository metadata is
# absent (a source archive, an image build context that omits .git). The
# identity carries no build time: it is a function of source state and
# configuration alone, so two builds of one tree agree.
# ==============================================================================

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
    message(FATAL_ERROR "GenerateBuildInfo: ${_required} is required")
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
