# ==============================================================================
# apex/Core.cmake - Foundation utilities for Apex CMake infrastructure
# ==============================================================================

include_guard(GLOBAL)

# ------------------------------------------------------------------------------
# apex_require(<VAR>...)
#
# Validate that all listed variables are defined and non-empty.
# Emits FATAL_ERROR with clear message if any are missing.
# ------------------------------------------------------------------------------
function (apex_require)
  foreach (_var IN LISTS ARGN)
    if (NOT DEFINED ${_var} OR "${${_var}}" STREQUAL "")
      message(FATAL_ERROR "apex_require: '${_var}' is required but not set")
    endif ()
  endforeach ()
endfunction ()

# ------------------------------------------------------------------------------
# apex_guard(<target>)
#
# Validate target exists. Emits FATAL_ERROR if not.
# ------------------------------------------------------------------------------
function (apex_guard _target)
  if (NOT TARGET "${_target}")
    message(FATAL_ERROR "apex_guard: target '${_target}' does not exist")
  endif ()
endfunction ()

# ------------------------------------------------------------------------------
# apex_module(<name>)
#
# Set standard module variables from naming convention:
#   LIB_NAME    = <name>
#   SRC_DIR     = ${CMAKE_CURRENT_SOURCE_DIR}/src
#   INC_DIR     = ${CMAKE_CURRENT_SOURCE_DIR}/inc
#   TST_DIR     = ${CMAKE_CURRENT_SOURCE_DIR}/tst
#   UTST_DIR    = ${CMAKE_CURRENT_SOURCE_DIR}/utst
#   PTST_DIR    = ${CMAKE_CURRENT_SOURCE_DIR}/ptst
#   DTST_DIR    = ${CMAKE_CURRENT_SOURCE_DIR}/dtst
#   README_FILE = ${CMAKE_CURRENT_SOURCE_DIR}/README.md
#
# Test directory conventions:
#   utst/ - Unit tests (run with make test)
#   ptst/ - Performance tests (run with make test, longer duration)
#   dtst/ - Development tests (manual execution only, external dependencies)
#   tst/  - General test bucket (legacy)
#
# All directories are optional. CMakeLists should check EXISTS before adding.
# ------------------------------------------------------------------------------
function (apex_module _name)
  set(LIB_NAME
      "${_name}"
      PARENT_SCOPE
  )
  set(SRC_DIR
      "${CMAKE_CURRENT_SOURCE_DIR}/src"
      PARENT_SCOPE
  )
  set(INC_DIR
      "${CMAKE_CURRENT_SOURCE_DIR}/inc"
      PARENT_SCOPE
  )
  set(TST_DIR
      "${CMAKE_CURRENT_SOURCE_DIR}/tst"
      PARENT_SCOPE
  )
  set(UTST_DIR
      "${CMAKE_CURRENT_SOURCE_DIR}/utst"
      PARENT_SCOPE
  )
  set(PTST_DIR
      "${CMAKE_CURRENT_SOURCE_DIR}/ptst"
      PARENT_SCOPE
  )
  set(DTST_DIR
      "${CMAKE_CURRENT_SOURCE_DIR}/dtst"
      PARENT_SCOPE
  )
  set(README_FILE
      "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
      PARENT_SCOPE
  )
endfunction ()

# ------------------------------------------------------------------------------
# apex_is_cuda_target(<target> <out_var>)
#
# Set <out_var> to TRUE if target has CUDA sources, FALSE otherwise.
# ------------------------------------------------------------------------------
function (apex_is_cuda_target _target _out_var)
  set(_has_cuda FALSE)

  get_target_property(_sources ${_target} SOURCES)
  if (_sources)
    foreach (_src IN LISTS _sources)
      if (_src MATCHES "\\.(cu|cuh)$")
        set(_has_cuda TRUE)
        break()
      endif ()
    endforeach ()
  endif ()

  set(${_out_var}
      ${_has_cuda}
      PARENT_SCOPE
  )
endfunction ()

# ------------------------------------------------------------------------------
# apex_standard_optins(<target>)
#
# Apply standard opt-in features to a target:
#   - apex_enable_coverage (Debug only, respects ENABLE_COVERAGE)
#   - apex_add_doxygen (respects PROJECT_BUILD_DOCS)
#   - apex_add_upx_copy (respects ENABLE_UPX)
#   - apex_clang_tidy_cuda (if target has CUDA sources, respects ENABLE_CLANG_TIDY)
#
# Safe to call on any target type. No-ops gracefully when features disabled
# or when target was skipped (e.g., due to bare-metal BAREMETAL guard).
# ------------------------------------------------------------------------------
function (apex_standard_optins _target)
  # Silently return if target doesn't exist (skipped by BAREMETAL guard)
  if (NOT TARGET "${_target}")
    return()
  endif ()

  # Coverage instrumentation
  apex_enable_coverage(${_target})

  # Doxygen documentation
  apex_add_doxygen(${_target})

  # UPX compression
  apex_add_upx_copy(${_target})

  # CUDA-specific: clang-tidy on .cu files
  apex_is_cuda_target(${_target} _has_cuda)
  if (_has_cuda)
    get_target_property(_sources ${_target} SOURCES)
    set(_cu_files)
    foreach (_src IN LISTS _sources)
      if (_src MATCHES "\\.(cu)$")
        list(APPEND _cu_files "${_src}")
      endif ()
    endforeach ()
    if (_cu_files)
      apex_clang_tidy_cuda(${_target} FILES ${_cu_files} ALLOW_FAILURE)
    endif ()
  endif ()
endfunction ()

# ------------------------------------------------------------------------------
# apex_add_test_subdirs(<dir_kind>...)
#
# Add test subdirectories that exist. Each argument names a test directory
# kind whose path variable was set by apex_module():
#   utst  -> ${UTST_DIR}    Unit tests (registered with CTest)
#   ptst  -> ${PTST_DIR}    Performance tests (manual execution)
#   dtst  -> ${DTST_DIR}    Development tests (manual execution)
#   tst   -> ${TST_DIR}     General test bucket
#
# Skipped on bare-metal builds (tests require host execution).
#
# Example:
#   apex_add_test_subdirs(utst ptst)       # Unit + perf tests
#   apex_add_test_subdirs(utst ptst dtst)  # Unit + perf + dev tests
#   apex_add_test_subdirs(tst)             # General bucket only
# ------------------------------------------------------------------------------
function (apex_add_test_subdirs)
  if (APEX_PLATFORM_BAREMETAL)
    return()
  endif ()

  foreach (_kind IN LISTS ARGN)
    if (_kind STREQUAL "utst")
      set(_dir "${UTST_DIR}")
    elseif (_kind STREQUAL "ptst")
      set(_dir "${PTST_DIR}")
    elseif (_kind STREQUAL "dtst")
      set(_dir "${DTST_DIR}")
    elseif (_kind STREQUAL "tst")
      set(_dir "${TST_DIR}")
    else ()
      message(WARNING "apex_add_test_subdirs: unknown kind '${_kind}'")
      continue()
    endif ()

    if (EXISTS "${_dir}/CMakeLists.txt")
      add_subdirectory("${_kind}")
    endif ()
  endforeach ()
endfunction ()

# ------------------------------------------------------------------------------
# Internal: Status/verbose helpers
# ------------------------------------------------------------------------------

# One-liner status (respects APEX_VERBOSE)
function (_apex_status _msg)
  if (APEX_VERBOSE)
    message(STATUS "[apex] ${_msg}")
  endif ()
endfunction ()

# Always-print status
function (_apex_info _msg)
  message(STATUS "[apex] ${_msg}")
endfunction ()
