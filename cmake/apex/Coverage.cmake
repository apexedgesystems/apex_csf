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

  # Stash the lib's source dir so the landing page can group it by area
  # (derived from filesystem layout, not hardcoded names).
  if (TARGET ${_lib})
    get_target_property(_src ${_lib} SOURCE_DIR)
    if (_src)
      set_property(GLOBAL APPEND PROPERTY APEX_COVERAGE_LIB_SOURCES "${_lib}|${_src}")
    endif ()
  endif ()
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

  # Compute area per lib from filesystem layout (dir under src/ or apps/).
  # This is the only place the build infra encodes layout, and it only knows
  # about the top-level roots -- everything beneath is auto-discovered.
  get_property(_lib_sources GLOBAL PROPERTY APEX_COVERAGE_LIB_SOURCES)
  set(_lib_areas "")
  foreach (_entry IN LISTS _lib_sources)
    string(REPLACE "|" ";" _parts "${_entry}")
    list(GET _parts 0 _lib)
    list(GET _parts 1 _src)
    file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}" "${_src}")
    if (_rel MATCHES "^([^/]+)/([^/]+)")
      # Area is "<root>/<group>" purely so libs sharing a root sort
      # contiguously on the landing page. We don't care what those root
      # names are -- they just keep apps from being interleaved with
      # library areas alphabetically. Display strips the root prefix.
      list(APPEND _lib_areas "${_lib}|${CMAKE_MATCH_1}/${CMAKE_MATCH_2}")
    endif ()
  endforeach ()

  # Write manifest
  set(_manifest "${APEX_COVERAGE_OUTPUT_DIR}/manifest.cmake")
  _apex_coverage_write_manifest("${_manifest}" "${_mappings}" "${_lib_areas}")

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
function (_apex_coverage_write_manifest _file _mappings _lib_areas)
  set(_content "# Coverage manifest (auto-generated)\n")
  string(APPEND _content "set(COVERAGE_MAPPINGS\n")
  foreach (_m IN LISTS _mappings)
    string(APPEND _content "  \"${_m}\"\n")
  endforeach ()
  string(APPEND _content ")\n")
  string(APPEND _content "set(COVERAGE_LIB_AREAS\n")
  foreach (_a IN LISTS _lib_areas)
    string(APPEND _content "  \"${_a}\"\n")
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

  # For header-only (INTERFACE) libraries there is no .so -- the coverage
  # data is compiled into the test binary itself.  Use -object only when
  # the shared library exists; otherwise llvm-cov reads the test binary.
  set(_object_args "")
  if (EXISTS "${_lib_so}")
    set(_object_args "-object=${_lib_so}")
  endif ()

  set(_out "${OUTPUT_DIR}/lib${_lib}")
  file(MAKE_DIRECTORY "${_out}/html")

  # HTML report
  execute_process(
    COMMAND "${LLVM_COV}" show "${_test_exe}"
      "-instr-profile=${_profdata}"
      "-format=html"
      "-output-dir=${_out}/html"
      "-ignore-filename-regex=${IGNORE_REGEX}"
      ${_object_args}
    OUTPUT_QUIET ERROR_QUIET
  )

  # Text summary
  execute_process(
    COMMAND "${LLVM_COV}" report "${_test_exe}"
      "-instr-profile=${_profdata}"
      "-ignore-filename-regex=${IGNORE_REGEX}"
      ${_object_args}
    OUTPUT_FILE "${_out}/summary.txt"
  )

  # LCOV for CI
  execute_process(
    COMMAND "${LLVM_COV}" export "${_test_exe}"
      "-instr-profile=${_profdata}"
      "-ignore-filename-regex=${IGNORE_REGEX}"
      ${_object_args}
      "-format=lcov"
    OUTPUT_FILE "${_out}/lcov.info"
    ERROR_QUIET
  )

  message(STATUS "[Coverage] Generated: ${_out}/html/index.html")
endforeach ()

# ------------------------------------------------------------------------------
# Landing page: aggregate per-library coverage into one index.html grouped by
# area. Each lib's area is derived from its filesystem position (the dir name
# directly under src/ or apps/), passed through via COVERAGE_LIB_AREAS in the
# manifest. The page has zero hardcoded module names -- new top-level dirs
# show up as new sections automatically.
#
# Areas live in dynamically-named buckets: ${_grp_<area>} holds the entries
# for area <area>. ${_areas} tracks which area names we've seen.
# ------------------------------------------------------------------------------
set(_areas "")

set(_repo_regions_t 0)
set(_repo_regions_m 0)
set(_repo_lines_t 0)
set(_repo_lines_m 0)

file(GLOB _summaries "${OUTPUT_DIR}/lib*/summary.txt")
foreach (_summary IN LISTS _summaries)
  get_filename_component(_lib_dir "${_summary}" DIRECTORY)
  get_filename_component(_lib_dirname "${_lib_dir}" NAME)
  string(REGEX REPLACE "^lib" "" _lib_name "${_lib_dirname}")

  file(READ "${_summary}" _content)
  string(REGEX MATCH "TOTAL[^\n]*" _total "${_content}")
  if (NOT _total)
    continue()
  endif ()

  # Cover columns are at positions 4, 7, 10, 13 (1-indexed) on the TOTAL row;
  # the line looks like:
  #   TOTAL  <regions> <missed> <cover%> <functions> <missed> <cover%> <lines>
  #          <missed> <cover%> <branches> <missed> <cover%>
  # Branches column may be `-` when there is no branch data.
  string(REGEX MATCHALL "[0-9]+\\.[0-9]+%|-" _all "${_total}")
  list(LENGTH _all _nfields)

  set(_region_pct "")
  set(_func_pct "")
  set(_line_pct "")
  set(_branch_pct "")
  if (_nfields GREATER_EQUAL 1)
    list(GET _all 0 _region_pct)
  endif ()
  if (_nfields GREATER_EQUAL 2)
    list(GET _all 1 _func_pct)
  endif ()
  if (_nfields GREATER_EQUAL 3)
    list(GET _all 2 _line_pct)
  endif ()
  if (_nfields GREATER_EQUAL 4)
    list(GET _all 3 _branch_pct)
  endif ()

  # Roll up raw region+line totals for the aggregate (functions/branches vary
  # in meaning across compilation units; line and region are the bedrock).
  string(REGEX MATCHALL "[0-9]+" _nums "${_total}")
  list(LENGTH _nums _nnums)
  if (_nnums GREATER_EQUAL 2)
    list(GET _nums 0 _r_total)
    list(GET _nums 1 _r_missed)
    math(EXPR _repo_regions_t "${_repo_regions_t} + ${_r_total}")
    math(EXPR _repo_regions_m "${_repo_regions_m} + ${_r_missed}")
  endif ()
  if (_nnums GREATER_EQUAL 8)
    list(GET _nums 6 _l_total)
    list(GET _nums 7 _l_missed)
    math(EXPR _repo_lines_t "${_repo_lines_t} + ${_l_total}")
    math(EXPR _repo_lines_m "${_repo_lines_m} + ${_l_missed}")
  endif ()

  # Look up filesystem-derived area for this lib (computed at configure time).
  # Strip _cuda suffix so CUDA variants group with their host counterparts.
  string(REGEX REPLACE "_cuda$" "" _area_key "${_lib_name}")
  set(_area "other")
  foreach (_la IN LISTS COVERAGE_LIB_AREAS)
    string(REPLACE "|" ";" _lap "${_la}")
    list(GET _lap 0 _lap_key)
    list(GET _lap 1 _lap_val)
    if (_lap_key STREQUAL _area_key OR _lap_key STREQUAL _lib_name)
      set(_area "${_lap_val}")
      break ()
    endif ()
  endforeach ()

  if (NOT "${_area}" IN_LIST _areas)
    list(APPEND _areas "${_area}")
  endif ()
  list(APPEND _grp_${_area} "${_lib_name}|${_region_pct}|${_func_pct}|${_line_pct}|${_branch_pct}")
endforeach ()

# Sort areas alphabetically, with "other" forced last.
list(SORT _areas)
if ("other" IN_LIST _areas)
  list(REMOVE_ITEM _areas "other")
  list(APPEND _areas "other")
endif ()

# Aggregate region / line %
set(_repo_region_pct "n/a")
set(_repo_line_pct "n/a")
if (_repo_regions_t GREATER 0)
  math(EXPR _repo_region_cov "(${_repo_regions_t} - ${_repo_regions_m}) * 10000 / ${_repo_regions_t}")
  math(EXPR _hi "${_repo_region_cov} / 100")
  math(EXPR _lo "${_repo_region_cov} % 100")
  if (_lo LESS 10)
    set(_lo "0${_lo}")
  endif ()
  set(_repo_region_pct "${_hi}.${_lo}%")
endif ()
if (_repo_lines_t GREATER 0)
  math(EXPR _repo_line_cov "(${_repo_lines_t} - ${_repo_lines_m}) * 10000 / ${_repo_lines_t}")
  math(EXPR _hi "${_repo_line_cov} / 100")
  math(EXPR _lo "${_repo_line_cov} % 100")
  if (_lo LESS 10)
    set(_lo "0${_lo}")
  endif ()
  set(_repo_line_pct "${_hi}.${_lo}%")
endif ()

# Render HTML
set(_html "<!DOCTYPE html>
<html lang=\"en\">
<head>
<meta charset=\"utf-8\">
<title>apex_csf coverage</title>
<style>
  body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; max-width: 1100px; margin: 40px auto; padding: 0 24px; color: #333; line-height: 1.5; }
  h1 { border-bottom: 2px solid #43a047; padding-bottom: 6px; }
  h2 { margin-top: 2em; color: #43a047; border-bottom: 1px solid #e0e0e0; padding-bottom: 4px; }
  table { border-collapse: collapse; width: 100%; margin-bottom: 1em; }
  th, td { padding: 6px 12px; text-align: left; border-bottom: 1px solid #eee; }
  th { background: #f5f5f5; font-weight: 600; }
  td.num { text-align: right; font-variant-numeric: tabular-nums; }
  td.lib { font-family: ui-monospace, SFMono-Regular, Menlo, monospace; }
  a { color: #2e7d32; text-decoration: none; }
  a:hover { text-decoration: underline; }
  .badge { display: inline-block; padding: 2px 8px; border-radius: 4px; font-size: 0.85em; font-weight: 600; min-width: 56px; text-align: center; }
  .ok   { background: #c8e6c9; color: #1b5e20; }
  .mid  { background: #fff9c4; color: #795548; }
  .low  { background: #ffcdd2; color: #b71c1c; }
  .na   { background: #eeeeee; color: #757575; }
  .aggregate { background: #e8f5e9; font-weight: 600; }
</style>
</head>
<body>
<h1>apex_csf coverage</h1>
<p>Per-library LLVM source-based coverage. Run <code>make compose-coverage</code> to refresh.</p>
<table>
  <tr class=\"aggregate\">
    <td>All libraries (aggregate)</td>
    <td class=\"num\">${_repo_region_pct} regions</td>
    <td class=\"num\">${_repo_line_pct} lines</td>
  </tr>
</table>
")

foreach (_area IN LISTS _areas)
  set(_libs_in_group ${_grp_${_area}})
  if (NOT _libs_in_group)
    continue ()
  endif ()
  list(SORT _libs_in_group)

  # Display title: strip root sort prefix, snake_case -> "Title Case".
  # "other" -> "Other".
  string(REGEX REPLACE "^[^/]+/" "" _area_display "${_area}")
  string(REPLACE "_" ";" _words "${_area_display}")
  set(_title "")
  foreach (_w IN LISTS _words)
    string(SUBSTRING "${_w}" 0 1 _head)
    string(SUBSTRING "${_w}" 1 -1 _tail)
    string(TOUPPER "${_head}" _head)
    if (_title)
      string(APPEND _title " ")
    endif ()
    string(APPEND _title "${_head}${_tail}")
  endforeach ()

  string(APPEND _html "<h2>${_title}</h2>
<table>
  <tr><th>Library</th><th>Regions</th><th>Functions</th><th>Lines</th><th>Branches</th></tr>
")
  foreach (_entry IN LISTS _libs_in_group)
    string(REPLACE "|" ";" _parts "${_entry}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _r)
    list(GET _parts 2 _f)
    list(GET _parts 3 _l)
    list(GET _parts 4 _b)

    # Per-cell badge class — each number gets its own tier.
    set(_cls_r "na")
    set(_cls_l "na")
    foreach (_pair IN ITEMS "_r;_cls_r" "_l;_cls_l")
      string(REPLACE ";" ";" _pv "${_pair}")
      list(GET _pv 0 _vname)
      list(GET _pv 1 _oname)
      set(_v "${${_vname}}")
      if (NOT _v STREQUAL "-" AND NOT _v STREQUAL "")
        string(REGEX REPLACE "%" "" _vnum "${_v}")
        string(REGEX REPLACE "\\..*" "" _vint "${_vnum}")
        if (_vint GREATER_EQUAL 80)
          set(${_oname} "ok")
        elseif (_vint GREATER_EQUAL 60)
          set(${_oname} "mid")
        else ()
          set(${_oname} "low")
        endif ()
      endif ()
    endforeach ()

    string(APPEND _html "  <tr>
    <td class=\"lib\"><a href=\"lib${_name}/html/index.html\">${_name}</a></td>
    <td class=\"num\"><span class=\"badge ${_cls_r}\">${_r}</span></td>
    <td class=\"num\">${_f}</td>
    <td class=\"num\"><span class=\"badge ${_cls_l}\">${_l}</span></td>
    <td class=\"num\">${_b}</td>
  </tr>
")
  endforeach ()
  string(APPEND _html "</table>
")
endforeach ()

string(APPEND _html "</body>
</html>
")

file(WRITE "${OUTPUT_DIR}/index.html" "${_html}")
message(STATUS "[Coverage] Landing page: ${OUTPUT_DIR}/index.html")
message(STATUS "[Coverage] Aggregate: ${_repo_region_pct} regions, ${_repo_line_pct} lines")

# Machine-readable status file for the coverage-check gate.
string(REGEX REPLACE "%" "" _repo_region_num "${_repo_region_pct}")
string(REGEX REPLACE "%" "" _repo_line_num "${_repo_line_pct}")
file(WRITE "${OUTPUT_DIR}/.coverage-status"
"REGION_COVERAGE=${_repo_region_num}
LINE_COVERAGE=${_repo_line_num}
LIB_COUNT=${_count}
")

message(STATUS "[Coverage] Reports: ${OUTPUT_DIR}")
]=]
  )
  file(WRITE "${_file}" "${_script}")
endfunction ()
