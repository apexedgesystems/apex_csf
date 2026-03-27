# ==============================================================================
# apex/Coverage.cmake - Code coverage infrastructure (LLVM source-based)
# ==============================================================================

include_guard(GLOBAL)

# ------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------

option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)

set(APEX_COVERAGE_LLVM_VERSION
    "21"
    CACHE STRING "LLVM tools version suffix (e.g., 21 for llvm-cov-21)"
)

set(APEX_COVERAGE_OUTPUT_DIR
    "${CMAKE_BINARY_DIR}/coverage"
    CACHE PATH "Coverage report output directory"
)

set(APEX_COVERAGE_IGNORE_REGEX
    ".*/build/_deps/.*|.*_uTest.*|.*_dTest.*|.*_pTest.*|/usr/local/.*"
    CACHE STRING "Regex for files to ignore in coverage reports"
)

# ------------------------------------------------------------------------------
# apex_enable_coverage(<target>)
#
# No-op for backward compatibility. Coverage is now opt-in via COVERAGE_FOR
# parameter in apex_add_gtest(). Called by apex_standard_optins.
# ------------------------------------------------------------------------------
function (apex_enable_coverage _target)
  # Coverage instrumentation is handled by apex_add_gtest COVERAGE_FOR
endfunction ()

# ------------------------------------------------------------------------------
# Internal state
# ------------------------------------------------------------------------------

define_property(
  GLOBAL
  PROPERTY APEX_COVERAGE_MAPPINGS
  BRIEF_DOCS "List of test:library mappings for coverage"
  FULL_DOCS "Semicolon-separated TEST_TARGET:LIBRARY_TARGET pairs"
)
set_property(GLOBAL PROPERTY APEX_COVERAGE_MAPPINGS "")

# ------------------------------------------------------------------------------
# apex_coverage_init()
#
# Initialize coverage infrastructure. Call once after project().
# Creates interface library for coverage flags and locates LLVM tools.
# ------------------------------------------------------------------------------
function (apex_coverage_init)
  if (NOT ENABLE_COVERAGE)
    return()
  endif ()

  # Coverage requires native execution
  if (CMAKE_CROSSCOMPILING)
    message(WARNING "[Coverage] Disabled for cross-compilation")
    set(ENABLE_COVERAGE
        OFF
        PARENT_SCOPE
    )
    return()
  endif ()

  if (APEX_PLATFORM_BAREMETAL)
    message(WARNING "[Coverage] Disabled for bare-metal builds")
    set(ENABLE_COVERAGE
        OFF
        PARENT_SCOPE
    )
    return()
  endif ()

  # LLVM coverage requires Clang
  if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(WARNING "[Coverage] Requires Clang (found: ${CMAKE_CXX_COMPILER_ID})")
    set(ENABLE_COVERAGE
        OFF
        PARENT_SCOPE
    )
    return()
  endif ()

  # Locate LLVM tools
  set(_ver "${APEX_COVERAGE_LLVM_VERSION}")

  find_program(APEX_LLVM_PROFDATA NAMES "llvm-profdata-${_ver}" "llvm-profdata")
  find_program(APEX_LLVM_COV NAMES "llvm-cov-${_ver}" "llvm-cov")

  if (NOT APEX_LLVM_PROFDATA OR NOT APEX_LLVM_COV)
    message(FATAL_ERROR "[Coverage] llvm-profdata/llvm-cov not found")
  endif ()

  # Interface library for coverage flags
  if (NOT TARGET apex_coverage_flags)
    add_library(apex_coverage_flags INTERFACE)
    add_library(apex::coverage_flags ALIAS apex_coverage_flags)

    target_compile_options(
      apex_coverage_flags
      INTERFACE $<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:CXX>>: -fprofile-instr-generate
                -fcoverage-mapping> $<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:C>>:
                -fprofile-instr-generate -fcoverage-mapping>
    )

    # Link options conditional on Clang linker (CUDA links with GCC)
    target_link_options(
      apex_coverage_flags
      INTERFACE
      $<$<AND:$<CONFIG:Debug>,$<LINK_LANG_AND_ID:CXX,Clang>>:
      -fprofile-instr-generate
      -fcoverage-mapping>
      $<$<AND:$<CONFIG:Debug>,$<LINK_LANG_AND_ID:C,Clang>>:
      -fprofile-instr-generate
      -fcoverage-mapping>
    )

    install(TARGETS apex_coverage_flags EXPORT apexTargets)
  endif ()

  message(STATUS "[Coverage] Enabled (LLVM ${_ver})")
  message(STATUS "[Coverage]   profdata: ${APEX_LLVM_PROFDATA}")
  message(STATUS "[Coverage]   cov:      ${APEX_LLVM_COV}")
  message(STATUS "[Coverage]   output:   ${APEX_COVERAGE_OUTPUT_DIR}")

  # Generate targets at end of configure
  cmake_language(DEFER CALL _apex_coverage_finalize)
endfunction ()

# ------------------------------------------------------------------------------
# apex_coverage_register(<test_target> <library_target>)
#
# Register test->library mapping. Called by apex_add_gtest when COVERAGE_FOR set.
# ------------------------------------------------------------------------------
function (apex_coverage_register _test _lib)
  if (NOT ENABLE_COVERAGE)
    return()
  endif ()

  set_property(GLOBAL APPEND PROPERTY APEX_COVERAGE_MAPPINGS "${_test}:${_lib}")
endfunction ()

# ------------------------------------------------------------------------------
# Internal: Generate coverage targets
# ------------------------------------------------------------------------------
function (_apex_coverage_finalize)
  if (NOT ENABLE_COVERAGE)
    return()
  endif ()

  # Guard against multiple invocations (e.g. FetchContent dependencies
  # that include the same Coverage.cmake)
  if (TARGET coverage-report)
    return()
  endif ()

  get_property(_mappings GLOBAL PROPERTY APEX_COVERAGE_MAPPINGS)
  if (NOT _mappings)
    message(STATUS "[Coverage] No mappings registered (use COVERAGE_FOR in apex_add_gtest)")
    return()
  endif ()

  list(LENGTH _mappings _count)
  message(STATUS "[Coverage] Generating targets for ${_count} mapping(s)")

  file(MAKE_DIRECTORY "${APEX_COVERAGE_OUTPUT_DIR}")

  # Write manifest
  set(_manifest "${APEX_COVERAGE_OUTPUT_DIR}/manifest.cmake")
  _apex_coverage_write_manifest("${_manifest}" "${_mappings}")

  # Write generator script
  set(_generator "${APEX_COVERAGE_OUTPUT_DIR}/generate_report.cmake")
  _apex_coverage_write_generator("${_generator}")

  # Target: coverage-report
  add_custom_target(
    coverage-report
    COMMAND ${CMAKE_COMMAND} -DMANIFEST=${_manifest} -P "${_generator}"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "[Coverage] Generating reports..."
    VERBATIM
  )

  # Target: coverage-clean
  add_custom_target(
    coverage-clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${APEX_COVERAGE_OUTPUT_DIR}"
    COMMAND find "${CMAKE_BINARY_DIR}" -name "*.profraw" -delete 2>/dev/null || true
    COMMENT "[Coverage] Cleaning artifacts..."
    VERBATIM
  )
endfunction ()

# ------------------------------------------------------------------------------
# Internal: Write manifest
# ------------------------------------------------------------------------------
function (_apex_coverage_write_manifest _file _mappings)
  set(_content "# Coverage manifest (auto-generated)\n")
  string(APPEND _content "set(COVERAGE_MAPPINGS\n")
  foreach (_m IN LISTS _mappings)
    string(APPEND _content "  \"${_m}\"\n")
  endforeach ()
  string(APPEND _content ")\n")
  string(APPEND _content "set(LLVM_PROFDATA \"${APEX_LLVM_PROFDATA}\")\n")
  string(APPEND _content "set(LLVM_COV \"${APEX_LLVM_COV}\")\n")
  string(APPEND _content "set(OUTPUT_DIR \"${APEX_COVERAGE_OUTPUT_DIR}\")\n")
  string(APPEND _content "set(IGNORE_REGEX \"${APEX_COVERAGE_IGNORE_REGEX}\")\n")
  string(APPEND _content "set(BUILD_DIR \"${CMAKE_BINARY_DIR}\")\n")
  string(APPEND _content "set(LIB_DIR \"${CMAKE_LIBRARY_OUTPUT_DIRECTORY}\")\n")
  string(APPEND _content "set(TEST_DIR \"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/tests\")\n")
  file(
    GENERATE
    OUTPUT "${_file}"
    CONTENT "${_content}"
  )
endfunction ()

# ------------------------------------------------------------------------------
# Internal: Write report generator script
# ------------------------------------------------------------------------------
function (_apex_coverage_write_generator _file)
  set(_script
      [=[
# Coverage report generator (auto-generated)
cmake_minimum_required(VERSION 3.20)

if (NOT DEFINED MANIFEST)
  message(FATAL_ERROR "MANIFEST not defined")
endif ()
include("${MANIFEST}")

# Find .profraw files
file(GLOB_RECURSE _profraw "${BUILD_DIR}/*.profraw")
list(LENGTH _profraw _count)

if (_count EQUAL 0)
  message(FATAL_ERROR "[Coverage] No .profraw files found. Run tests first.")
endif ()

message(STATUS "[Coverage] Found ${_count} .profraw file(s)")

# Merge into single profdata
set(_profdata "${OUTPUT_DIR}/merged.profdata")
execute_process(
  COMMAND "${LLVM_PROFDATA}" merge -sparse ${_profraw} -o "${_profdata}"
  RESULT_VARIABLE _rc
)
if (NOT _rc EQUAL 0)
  message(FATAL_ERROR "[Coverage] profdata merge failed")
endif ()

message(STATUS "[Coverage] Merged: ${_profdata}")

# Generate per-module reports
foreach (_mapping IN LISTS COVERAGE_MAPPINGS)
  string(REPLACE ":" ";" _parts "${_mapping}")
  list(GET _parts 0 _test)
  list(GET _parts 1 _lib)

  set(_test_exe "${TEST_DIR}/${_test}")
  set(_lib_so "${LIB_DIR}/lib${_lib}.so")

  if (NOT EXISTS "${_test_exe}")
    message(WARNING "[Coverage] Test not found: ${_test_exe}")
    continue()
  endif ()

  if (NOT EXISTS "${_lib_so}")
    message(WARNING "[Coverage] Library not found: ${_lib_so}")
    continue()
  endif ()

  set(_out "${OUTPUT_DIR}/${_lib}")
  file(MAKE_DIRECTORY "${_out}/html")

  # HTML report
  execute_process(
    COMMAND "${LLVM_COV}" show "${_test_exe}"
      "-instr-profile=${_profdata}"
      "-format=html"
      "-output-dir=${_out}/html"
      "-ignore-filename-regex=${IGNORE_REGEX}"
      "-object=${_lib_so}"
    OUTPUT_QUIET ERROR_QUIET
  )

  # Text summary
  execute_process(
    COMMAND "${LLVM_COV}" report "${_test_exe}"
      "-instr-profile=${_profdata}"
      "-ignore-filename-regex=${IGNORE_REGEX}"
      "-object=${_lib_so}"
    OUTPUT_FILE "${_out}/summary.txt"
  )

  # LCOV for CI
  execute_process(
    COMMAND "${LLVM_COV}" export "${_test_exe}"
      "-instr-profile=${_profdata}"
      "-ignore-filename-regex=${IGNORE_REGEX}"
      "-object=${_lib_so}"
      "-format=lcov"
    OUTPUT_FILE "${_out}/lcov.info"
    ERROR_QUIET
  )

  message(STATUS "[Coverage] Generated: ${_out}/html/index.html")
endforeach ()

message(STATUS "[Coverage] Reports: ${OUTPUT_DIR}")
]=]
  )
  file(WRITE "${_file}" "${_script}")
endfunction ()
