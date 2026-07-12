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
      INTERFACE $<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:CXX>>:
                -fprofile-instr-generate
                -fcoverage-mapping
                -fcoverage-mcdc>
                $<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:C>>:
                -fprofile-instr-generate
                -fcoverage-mapping
                -fcoverage-mcdc>
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

  # Compute area per lib from filesystem layout (dir under src/, demos/, or apps/).
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
  # Per-library source dirs (dedup: a lib registered by several tests appears
  # once) let each library's report scope to its own sources.
  set(_lib_dirs "${_lib_sources}")
  list(REMOVE_DUPLICATES _lib_dirs)
  _apex_coverage_write_manifest("${_manifest}" "${_mappings}" "${_lib_areas}" "${_lib_dirs}")

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
function (_apex_coverage_write_manifest _file _mappings _lib_areas _lib_dirs)
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
  # lib|abs-source-dir, used to scope each library's report to its own sources.
  string(APPEND _content "set(COVERAGE_LIB_SOURCES\n")
  foreach (_s IN LISTS _lib_dirs)
    string(APPEND _content "  \"${_s}\"\n")
  endforeach ()
  string(APPEND _content ")\n")
  string(APPEND _content "set(LLVM_PROFDATA \"${APEX_LLVM_PROFDATA}\")\n")
  string(APPEND _content "set(LLVM_COV \"${APEX_LLVM_COV}\")\n")
  string(APPEND _content "set(OUTPUT_DIR \"${APEX_COVERAGE_OUTPUT_DIR}\")\n")
  string(APPEND _content "set(IGNORE_REGEX \"${APEX_COVERAGE_IGNORE_REGEX}\")\n")
  string(APPEND _content "set(BUILD_DIR \"${CMAKE_BINARY_DIR}\")\n")
  string(APPEND _content "set(LIB_DIR \"${CMAKE_LIBRARY_OUTPUT_DIRECTORY}\")\n")
  # Coverage is measured from unit tests only; they live in bin/utests/ (the
  # per-test-type output dir). ptest/dtest do not register coverage mappings.
  string(APPEND _content "set(TEST_DIR \"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/utests\")\n")
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

# Format covered/count as an "NN.NN%" string (integer math; "-" when count 0).
function (_pct _v _c _out)
  if (_c GREATER 0)
    math(EXPR _p "(${_v} * 10000) / ${_c}")
    math(EXPR _hi "${_p} / 100")
    math(EXPR _lo "${_p} % 100")
    if (_lo LESS 10)
      set(_lo "0${_lo}")
    endif ()
    set(${_out} "${_hi}.${_lo}%" PARENT_SCOPE)
  else ()
    set(${_out} "-" PARENT_SCOPE)
  endif ()
endfunction ()

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

# Single-pass union coverage.
#
# Every covered file is measured once, from the merged profile of all unit tests
# (the union): a library is credited for every test that exercises its code, and
# each source file is counted exactly once -- no per-include header duplication,
# no per-test under-crediting. One `llvm-cov export` emits per-file summaries for
# the whole tree; the per-library table and the aggregate are both derived from
# it by attributing each file to the library whose source directory owns it.
#
# lib -> own source dir; the longest dir that prefixes a file path owns the file.
set(_lib_dirs "")
foreach (_entry IN LISTS COVERAGE_LIB_SOURCES)
  string(REPLACE "|" ";" _sp "${_entry}")
  list(GET _sp 0 _slib)
  list(GET _sp 1 _sdir)
  set("_SRCDIR_${_slib}" "${_sdir}")
  list(APPEND _lib_dirs "${_slib}")
endforeach ()

# Object set: every instrumented unit-test binary (unique) plus its shared
# library. The first test is llvm-cov's positional binary, the rest are -object;
# passing every binary lets each covered function resolve regardless of which
# test exercised it.
set(_all_tests "")
set(_reg_libs "")
foreach (_mapping IN LISTS COVERAGE_MAPPINGS)
  string(REPLACE ":" ";" _parts "${_mapping}")
  list(GET _parts 0 _test)
  list(GET _parts 1 _lib)
  if (NOT "${_test}" IN_LIST _all_tests)
    list(APPEND _all_tests "${_test}")
  endif ()
  if (NOT "${_lib}" IN_LIST _reg_libs)
    list(APPEND _reg_libs "${_lib}")
  endif ()
endforeach ()

set(_main "")
set(_objs "")
foreach (_test IN LISTS _all_tests)
  set(_test_exe "${TEST_DIR}/${_test}")
  if (NOT EXISTS "${_test_exe}")
    message(WARNING "[Coverage] Test not found: ${_test_exe}")
    continue()
  endif ()
  if (NOT _main)
    set(_main "${_test_exe}")
  else ()
    list(APPEND _objs "-object=${_test_exe}")
  endif ()
endforeach ()
foreach (_lib IN LISTS _reg_libs)
  set(_lib_so "${LIB_DIR}/lib${_lib}.so")
  # Header-only (INTERFACE) libs have no .so; their code lives in the test binary.
  if (EXISTS "${_lib_so}")
    list(APPEND _objs "-object=${_lib_so}")
  endif ()
endforeach ()
if (NOT _main)
  message(FATAL_ERROR "[Coverage] No usable test binaries for the report")
endif ()

# One annotated HTML report over every binary -- each source file rendered once.
file(MAKE_DIRECTORY "${OUTPUT_DIR}/html")
execute_process(
  COMMAND "${LLVM_COV}" show "${_main}" ${_objs}
    "-instr-profile=${_profdata}"
    "-format=html"
    "-show-mcdc"
    "-output-dir=${OUTPUT_DIR}/html"
    "-ignore-filename-regex=${IGNORE_REGEX}"
  OUTPUT_QUIET ERROR_QUIET
)
message(STATUS "[Coverage] Annotated report: ${OUTPUT_DIR}/html/index.html")

# One aggregate LCOV export over the union (downloadable with the report
# artifact; consumed by no workflow today, but kept as a standard interchange
# format for external coverage tooling).
execute_process(
  COMMAND "${LLVM_COV}" export "${_main}" ${_objs}
    "-instr-profile=${_profdata}"
    "-ignore-filename-regex=${IGNORE_REGEX}"
    "-format=lcov"
  OUTPUT_FILE "${OUTPUT_DIR}/lcov.info"
  ERROR_QUIET
)

# One per-file summary export drives every per-library and aggregate number.
# stderr is quieted: a whole-tree union over every binary reports functions whose
# structural hash differs across the binaries they were inlined into ("N
# functions have mismatched data"). The matching records are still applied, so
# the counted coverage is unaffected (the export totals equal an independent
# union report); the warning would otherwise be non-actionable noise every run.
set(_summary_json "${OUTPUT_DIR}/summary.json")
execute_process(
  COMMAND "${LLVM_COV}" export "${_main}" ${_objs}
    "-instr-profile=${_profdata}"
    "-ignore-filename-regex=${IGNORE_REGEX}"
    "-summary-only" "-format=text"
  OUTPUT_FILE "${_summary_json}"
  RESULT_VARIABLE _rc
  ERROR_QUIET
)
if (NOT _rc EQUAL 0)
  message(FATAL_ERROR "[Coverage] summary export failed")
endif ()

# Attribute each file's per-metric counts to its owning library. Files under no
# registered library's source dir are pooled under "__unattributed__" so they
# still count in the aggregate and are reported, never silently dropped.
file(READ "${_summary_json}" _json)
string(JSON _nfiles LENGTH "${_json}" "data" 0 "files")
set(_metrics regions functions lines branches mcdc)
set(_seen_libs "")
math(EXPR _last "${_nfiles} - 1")
foreach (_i RANGE ${_last})
  string(JSON _fn GET "${_json}" "data" 0 "files" ${_i} "filename")
  string(JSON _s GET "${_json}" "data" 0 "files" ${_i} "summary")

  # Owning library = longest source dir that is a path prefix of this file.
  set(_owner "__unattributed__")
  set(_owner_len 0)
  foreach (_lib IN LISTS _lib_dirs)
    set(_d "${_SRCDIR_${_lib}}")
    string(FIND "${_fn}" "${_d}/" _at)
    if (_at EQUAL 0)
      string(LENGTH "${_d}" _dl)
      if (_dl GREATER _owner_len)
        set(_owner "${_lib}")
        set(_owner_len ${_dl})
      endif ()
    endif ()
  endforeach ()

  if (NOT "${_owner}" IN_LIST _seen_libs)
    list(APPEND _seen_libs "${_owner}")
    foreach (_m IN LISTS _metrics)
      set("_L_${_owner}_${_m}_c" 0)
      set("_L_${_owner}_${_m}_v" 0)
    endforeach ()
  endif ()
  foreach (_m IN LISTS _metrics)
    string(JSON _c ERROR_VARIABLE _e GET "${_s}" "${_m}" "count")
    if (_e)
      set(_c 0)
    endif ()
    string(JSON _v ERROR_VARIABLE _e GET "${_s}" "${_m}" "covered")
    if (_e)
      set(_v 0)
    endif ()
    math(EXPR _nc "${_L_${_owner}_${_m}_c} + ${_c}")
    math(EXPR _nv "${_L_${_owner}_${_m}_v} + ${_v}")
    set("_L_${_owner}_${_m}_c" "${_nc}")
    set("_L_${_owner}_${_m}_v" "${_nv}")
  endforeach ()
endforeach ()

if ("__unattributed__" IN_LIST _seen_libs)
  set(_uname "__unattributed__")
  message(STATUS "[Coverage] ${_L_${_uname}_lines_c} lines across files under no registered "
    "library counted in the aggregate (shown as the 'unattributed' row).")
endif ()

# ------------------------------------------------------------------------------
# Landing page: aggregate per-library coverage into one index.html grouped by
# area. Each lib's area is derived from its filesystem position (the dir name
# directly under src/, demos/, or apps/), passed through via COVERAGE_LIB_AREAS in the
# manifest. The page has zero hardcoded module names -- new top-level dirs
# show up as new sections automatically.
#
# Areas live in dynamically-named buckets: ${_grp_<area>} holds the entries
# for area <area>. ${_areas} tracks which area names we've seen.
# ------------------------------------------------------------------------------
set(_areas "")

# Per-library table rows from the attribution accumulators. Each metric's % is
# covered/count for the files the library owns. The "unattributed" pool (files
# under no registered library) renders as its own row under "other".
foreach (_lib IN LISTS _seen_libs)
  _pct("${_L_${_lib}_regions_v}" "${_L_${_lib}_regions_c}" _region_pct)
  _pct("${_L_${_lib}_functions_v}" "${_L_${_lib}_functions_c}" _func_pct)
  _pct("${_L_${_lib}_lines_v}" "${_L_${_lib}_lines_c}" _line_pct)
  _pct("${_L_${_lib}_branches_v}" "${_L_${_lib}_branches_c}" _branch_pct)
  _pct("${_L_${_lib}_mcdc_v}" "${_L_${_lib}_mcdc_c}" _mcdc_pct)

  if (_lib STREQUAL "__unattributed__")
    set(_lib_name "unattributed")
    set(_area "other")
  else ()
    set(_lib_name "${_lib}")
    # Filesystem-derived area (computed at configure time). Strip _cuda suffix so
    # CUDA variants group with their host counterparts.
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
  endif ()

  if (NOT "${_area}" IN_LIST _areas)
    list(APPEND _areas "${_area}")
  endif ()
  list(APPEND _grp_${_area} "${_lib_name}|${_region_pct}|${_func_pct}|${_line_pct}|${_branch_pct}|${_mcdc_pct}")
endforeach ()

# Sort areas alphabetically, with "other" forced last.
list(SORT _areas)
if ("other" IN_LIST _areas)
  list(REMOVE_ITEM _areas "other")
  list(APPEND _areas "other")
endif ()

# Aggregate region / line / MC/DC from the export's tree-wide totals -- every
# file counted once. Taken from the totals object rather than re-summed, so the
# headline number cannot drift from the per-file data.
string(JSON _tot GET "${_json}" "data" 0 "totals")
string(JSON _agg_rv ERROR_VARIABLE _e GET "${_tot}" "regions" "covered")
string(JSON _agg_rc ERROR_VARIABLE _e GET "${_tot}" "regions" "count")
string(JSON _agg_lv ERROR_VARIABLE _e GET "${_tot}" "lines" "covered")
string(JSON _agg_lc ERROR_VARIABLE _e GET "${_tot}" "lines" "count")
string(JSON _agg_mv ERROR_VARIABLE _e GET "${_tot}" "mcdc" "covered")
string(JSON _agg_mc ERROR_VARIABLE _e GET "${_tot}" "mcdc" "count")
_pct("${_agg_rv}" "${_agg_rc}" _repo_region_pct)
_pct("${_agg_lv}" "${_agg_lc}" _repo_line_pct)
_pct("${_agg_mv}" "${_agg_mc}" _repo_mcdc_pct)

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
<p><a href=\"html/index.html\">&#8594; Full annotated source report</a> (line-by-line coverage, branches, and MC/DC).</p>
<table>
  <tr class=\"aggregate\">
    <td>All libraries (aggregate)</td>
    <td class=\"num\">${_repo_region_pct} regions</td>
    <td class=\"num\">${_repo_line_pct} lines</td>
    <td class=\"num\">${_repo_mcdc_pct} MC/DC</td>
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
  <tr><th>Library</th><th>Regions</th><th>Functions</th><th>Lines</th><th>Branches</th><th>MC/DC</th></tr>
")
  foreach (_entry IN LISTS _libs_in_group)
    string(REPLACE "|" ";" _parts "${_entry}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _r)
    list(GET _parts 2 _f)
    list(GET _parts 3 _l)
    list(GET _parts 4 _b)
    set(_m "")
    list(LENGTH _parts _nparts)
    if (_nparts GREATER_EQUAL 6)
      list(GET _parts 5 _m)
    endif ()

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
    <td class=\"lib\">${_name}</td>
    <td class=\"num\"><span class=\"badge ${_cls_r}\">${_r}</span></td>
    <td class=\"num\">${_f}</td>
    <td class=\"num\"><span class=\"badge ${_cls_l}\">${_l}</span></td>
    <td class=\"num\">${_b}</td>
    <td class=\"num\">${_m}</td>
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
message(STATUS "[Coverage] Aggregate: ${_repo_region_pct} regions, ${_repo_line_pct} lines, ${_repo_mcdc_pct} MC/DC")

# Count of libraries actually scored (the unattributed pool is not a library).
set(_lib_count 0)
foreach (_lib IN LISTS _seen_libs)
  if (NOT _lib STREQUAL "__unattributed__")
    math(EXPR _lib_count "${_lib_count} + 1")
  endif ()
endforeach ()

# Machine-readable status file for the coverage-check gate.
string(REGEX REPLACE "%" "" _repo_region_num "${_repo_region_pct}")
string(REGEX REPLACE "%" "" _repo_line_num "${_repo_line_pct}")
string(REGEX REPLACE "%" "" _repo_mcdc_num "${_repo_mcdc_pct}")
file(WRITE "${OUTPUT_DIR}/.coverage-status"
"REGION_COVERAGE=${_repo_region_num}
LINE_COVERAGE=${_repo_line_num}
MCDC_COVERAGE=${_repo_mcdc_num}
LIB_COUNT=${_lib_count}
")

message(STATUS "[Coverage] Reports: ${OUTPUT_DIR}")
]=]
  )
  file(WRITE "${_file}" "${_script}")
endfunction ()
