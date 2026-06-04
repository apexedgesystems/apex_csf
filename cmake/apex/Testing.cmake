# ==============================================================================
# apex/Testing.cmake - Test infrastructure and coverage support
# ==============================================================================
#
# Three test types, each a distinct build config. All three build a
# GTest/GMock executable from the shared core (_apex_test_executable); they
# differ only in the wrapper's added config:
#
#   Type  Dir         CTest?  Coverage  PCH  Extra                 Helper
#   ----  ----------  ------  --------  ---  --------------------  -----------------
#   utst  bin/utests/ yes     yes       yes  labels/timing/locks   apex_add_gtest
#   ptst  bin/ptests/ no      no        no   gperftools + vernier  apex_add_ptest
#                                            + rpath fixup
#   dtst  bin/dtests/ no      no        no   (none)                apex_add_devtest
#
# - utst: unit tests -- exercise one class/function in isolation. Registered
#         with CTest and run by `make test`.
# - ptst: performance/benchmark tests -- measure throughput/latency. Run
#         manually (bin/ptests/<name>), never gated in CI.
# - dtst: component-level tests -- exercise a whole component or its
#         integration with others, too coarse/slow for the unit gate. Run
#         manually (bin/dtests/<name>).
# ==============================================================================

include_guard(GLOBAL)

# ------------------------------------------------------------------------------
# _apex_test_executable(TARGET <t> OUTDIR <subdir> SOURCES <src...>
#                       [INC <dir>] [CUDA <cu...>] [LINK <libs...>]
#                       [EXTRA_LINK <libs...>] [PCH])
#
# Shared core for every test type: create a GTest/GMock-linked executable,
# attach CUDA sources, and route the binary to bin/<OUTDIR>/. What each test
# type adds on top (CTest registration, coverage, rpath, perf libs) lives in
# the public wrapper below -- that is what makes the types distinct.
# ------------------------------------------------------------------------------
function (_apex_test_executable)
  cmake_parse_arguments(TE "PCH" "TARGET;INC;OUTDIR" "SOURCES;CUDA;LINK;EXTRA_LINK" ${ARGN})

  add_executable(${TE_TARGET})
  target_sources(${TE_TARGET} PRIVATE ${TE_SOURCES})

  if (TE_PCH)
    apex_pch_apply(${TE_TARGET} TEST)
  endif ()

  if (TE_INC)
    target_include_directories(${TE_TARGET} PRIVATE "${TE_INC}")
  endif ()

  if (TE_CUDA
      AND CUDAToolkit_FOUND
      AND CMAKE_CUDA_COMPILER
  )
    apex_cuda_sources(${TE_TARGET} FILES ${TE_CUDA})
  endif ()

  # GTest linkage (resolved once project-wide; this is a fallback).
  if (NOT TARGET GTest::gtest_main)
    find_package(GTest QUIET CONFIG)
    if (NOT TARGET GTest::gtest_main)
      find_package(GTest QUIET)
    endif ()
    if (NOT TARGET GTest::gtest_main)
      message(FATAL_ERROR "_apex_test_executable: GTest::gtest_main not found")
    endif ()
  endif ()

  target_link_libraries(
    ${TE_TARGET} PRIVATE GTest::gtest_main GTest::gmock ${TE_LINK} ${TE_EXTRA_LINK}
  )

  set_target_properties(
    ${TE_TARGET} PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TE_OUTDIR}"
  )
endfunction ()

# ------------------------------------------------------------------------------
# apex_add_gtest(...)  -- UNIT test (utst/), registered with CTest
#
# Add a GoogleTest-based test target with coverage and timing controls.
#
# Coverage is automatic: any project library in LINK is instrumented when
# ENABLE_COVERAGE=ON. Use COVERAGE_FOR only to override auto-detection.
#
# Arguments:
#   TARGET          <n>              required
#   SOURCES         <src...>         required
#   CUDA            <cu...>          optional
#   LINK            <libs...>        optional
#   COVERAGE_FOR    <lib_target>     optional (overrides auto-detection)
#   INC             <dir>            optional
#   LABELS          <labels...>      optional
#   WORKING_DIR     <dir>            optional
#   RESOURCE_LOCK   <n>              optional
#   NO_COVERAGE                      optional flag (skip coverage)
#   NO_PCH                           optional flag (skip precompiled headers)
#   TIMING_ALL                       optional flag
#   TIMING_TESTS    <names...>       optional
#   TIMING_PATTERNS <regex...>       optional
#   REQUIRES_THREADS <n>             optional (min hardware threads required)
# ------------------------------------------------------------------------------
function (apex_add_gtest)
  # Skip on bare-metal
  if (APEX_PLATFORM_BAREMETAL)
    return()
  endif ()

  cmake_parse_arguments(
    GT "TIMING_ALL;NO_COVERAGE;NO_PCH"
    "TARGET;INC;WORKING_DIR;RESOURCE_LOCK;COVERAGE_FOR;REQUIRES_THREADS"
    "SOURCES;CUDA;LINK;LABELS;TIMING_TESTS;TIMING_PATTERNS" ${ARGN}
  )
  apex_require(GT_TARGET GT_SOURCES)

  # Unit-test config: bin/utests/, precompiled headers on by default.
  set(_pch "PCH")
  if (GT_NO_PCH)
    set(_pch "")
  endif ()
  _apex_test_executable(
    TARGET
    ${GT_TARGET}
    OUTDIR
    utests
    SOURCES
    ${GT_SOURCES}
    CUDA
    ${GT_CUDA}
    LINK
    ${GT_LINK}
    INC
    "${GT_INC}"
    ${_pch}
  )

  # Coverage instrumentation (auto-detect from LINK or use COVERAGE_FOR override)
  set(_coverage_libs "")
  if (GT_NO_COVERAGE)
    # Skip coverage entirely (used by apex_add_ptest)
  elseif (ENABLE_COVERAGE AND TARGET apex_coverage_flags)
    if (GT_COVERAGE_FOR)
      # Explicit override
      set(_coverage_libs ${GT_COVERAGE_FOR})
    else ()
      # Auto-detect from LINK: filter out imported and aliased targets
      foreach (_lib IN LISTS GT_LINK)
        if (NOT TARGET ${_lib})
          continue()
        endif ()
        # Skip aliased targets (apex::* or any ALIAS)
        if (_lib MATCHES "^apex::")
          continue()
        endif ()
        get_target_property(_aliased ${_lib} ALIASED_TARGET)
        if (_aliased)
          continue()
        endif ()
        # Skip imported targets (third-party like fmt::fmt, GTest::*)
        get_target_property(_imported ${_lib} IMPORTED)
        if (_imported)
          continue()
        endif ()
        list(APPEND _coverage_libs ${_lib})
      endforeach ()
    endif ()

    # Instrument test executable and libraries
    if (_coverage_libs)
      target_link_libraries(${GT_TARGET} PRIVATE apex::coverage_flags)

      foreach (_lib IN LISTS _coverage_libs)
        if (TARGET ${_lib})
          get_target_property(_lib_type ${_lib} TYPE)
          if (_lib_type STREQUAL "INTERFACE_LIBRARY")
            target_link_libraries(${_lib} INTERFACE apex::coverage_flags)
          else ()
            target_link_libraries(${_lib} PRIVATE apex::coverage_flags)
          endif ()

          # Register mapping for report generation
          if (COMMAND apex_coverage_register)
            apex_coverage_register(${GT_TARGET} ${_lib})
          endif ()
        endif ()
      endforeach ()
    endif ()
  endif ()

  # Test discovery
  include(GoogleTest)
  if (GT_WORKING_DIR)
    set(_working_dir "${GT_WORKING_DIR}")
  else ()
    set(_working_dir "${CMAKE_CURRENT_BINARY_DIR}")
  endif ()

  set(_discover_sources ${GT_SOURCES})
  if (GT_CUDA
      AND CUDAToolkit_FOUND
      AND CMAKE_CUDA_COMPILER
  )
    list(APPEND _discover_sources ${GT_CUDA})
  endif ()

  set(_all_tests "")
  gtest_add_tests(
    TARGET ${GT_TARGET}
    SOURCES ${_discover_sources}
    WORKING_DIRECTORY "${_working_dir}"
    TEST_LIST _all_tests
  )

  # Base labels
  if (GT_LABELS)
    set_property(
      TEST ${_all_tests}
      APPEND
      PROPERTY LABELS "${GT_LABELS}"
    )
  endif ()

  # Set working directory for all discovered tests
  set_tests_properties(${_all_tests} PROPERTIES WORKING_DIRECTORY "${_working_dir}")

  # Resource lock for tests that share system resources (ports, files, etc.)
  # Applied to ALL tests in the target, independent of timing sensitivity
  if (GT_RESOURCE_LOCK AND _all_tests)
    set_tests_properties(${_all_tests} PROPERTIES RESOURCE_LOCK "${GT_RESOURCE_LOCK}")
  endif ()

  # Hardware thread requirement - skip tests on systems with insufficient threads
  if (GT_REQUIRES_THREADS AND _all_tests)
    cmake_host_system_information(RESULT _hw_threads QUERY NUMBER_OF_LOGICAL_CORES)
    if (_hw_threads LESS GT_REQUIRES_THREADS)
      message(
        STATUS
          "[Skip] ${GT_TARGET}: requires ${GT_REQUIRES_THREADS} threads, system has ${_hw_threads}"
      )
      set_tests_properties(${_all_tests} PROPERTIES DISABLED TRUE)
    endif ()
  endif ()

  # Timing-sensitive test handling
  set(_timed_tests "")
  if (GT_TIMING_ALL)
    set(_timed_tests "${_all_tests}")
  else ()
    if (GT_TIMING_TESTS)
      foreach (_t IN LISTS GT_TIMING_TESTS)
        list(FIND _all_tests "${_t}" _idx)
        if (_idx GREATER -1)
          list(APPEND _timed_tests "${_t}")
        endif ()
      endforeach ()
    endif ()
    if (GT_TIMING_PATTERNS)
      foreach (_name IN LISTS _all_tests)
        foreach (_rx IN LISTS GT_TIMING_PATTERNS)
          if (_name MATCHES "${_rx}")
            list(APPEND _timed_tests "${_name}")
            break()
          endif ()
        endforeach ()
      endforeach ()
    endif ()
    list(REMOVE_DUPLICATES _timed_tests)
  endif ()

  if (_timed_tests)
    set_property(
      TEST ${_timed_tests}
      APPEND
      PROPERTY LABELS "Timing"
    )
    # Timing tests run serially unless RESOURCE_LOCK was already applied above
    if (NOT GT_RESOURCE_LOCK)
      set_tests_properties(${_timed_tests} PROPERTIES RUN_SERIAL TRUE)
    endif ()
  endif ()

  # Coverage test (when any coverage libraries detected)
  if (_coverage_libs)
    set(_cov_labels "Coverage")
    if (GT_LABELS)
      list(APPEND _cov_labels ${GT_LABELS})
    endif ()

    add_test(NAME ${GT_TARGET}_coverage COMMAND $<TARGET_FILE:${GT_TARGET}>)
    set_tests_properties(
      ${GT_TARGET}_coverage
      PROPERTIES LABELS "${_cov_labels}" WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" ENVIRONMENT
                 "LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/${GT_TARGET}.coverage.profraw"
    )
  endif ()

  # Summary
  list(LENGTH _all_tests _disc_len)
  list(LENGTH _timed_tests _timed_len)
  set(_req_threads_msg "")
  if (GT_REQUIRES_THREADS)
    set(_req_threads_msg " threads=${GT_REQUIRES_THREADS}")
  endif ()
  message(STATUS "[Test] target=${GT_TARGET} tests=${_disc_len} timed=${_timed_len} "
                 "lock='${GT_RESOURCE_LOCK}'${_req_threads_msg}"
  )
endfunction ()

# ------------------------------------------------------------------------------
# apex_add_ptest(...)  -- PERFORMANCE test (ptests/), NOT registered with CTest
#
# Add a performance test executable that is built but NOT registered with CTest.
# Performance tests are for benchmarking and should be run manually.
#
# The executable is placed in bin/ptests/ and can be run manually with:
#   ./build/hosted-x86_64-debug/bin/ptests/TestName --csv results.csv
#
# Features:
#   - Output to bin/ptests/
#   - Auto-links benchmarking library (Vernier) if available
#   - Auto-links gperftools if available
#   - RPATH fixup so the binary finds project shared libs from bin/ptests/
#
# Arguments:
#   TARGET          <n>              required
#   SOURCES         <src...>         required
#   CUDA            <cu...>          optional
#   LINK            <libs...>        optional
#   INC             <dir>            optional
# ------------------------------------------------------------------------------
function (apex_add_ptest)
  # Skip on bare-metal
  if (APEX_PLATFORM_BAREMETAL)
    return()
  endif ()

  # Note: LABELS is parsed but ignored (ptests not registered with CTest)
  cmake_parse_arguments(PT "" "TARGET;INC" "SOURCES;CUDA;LINK;LABELS" ${ARGN})
  apex_require(PT_TARGET PT_SOURCES)

  # Perf-test extra links: gperftools (optional) + Vernier bench (if present).
  set(_perf_link "")
  find_library(GPERF_PROFILER_LIB NAMES profiler)
  find_library(GPERF_TCMALLOC_LIB NAMES tcmalloc)
  if (GPERF_PROFILER_LIB)
    list(APPEND _perf_link "${GPERF_PROFILER_LIB}")
  endif ()
  if (GPERF_TCMALLOC_LIB)
    list(APPEND _perf_link "${GPERF_TCMALLOC_LIB}")
  endif ()
  list(APPEND _perf_link $<$<TARGET_EXISTS:vernier::bench>:vernier::bench>)

  # Perf-test config: bin/ptests/, perf libs, no PCH/coverage.
  _apex_test_executable(
    TARGET
    ${PT_TARGET}
    OUTDIR
    ptests
    SOURCES
    ${PT_SOURCES}
    CUDA
    ${PT_CUDA}
    LINK
    ${PT_LINK}
    INC
    "${PT_INC}"
    EXTRA_LINK
    ${_perf_link}
  )

  # RPATH fixup for ptests layout
  set(_ptests_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/ptests")
  set(_lib_dir "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
  if (NOT _lib_dir)
    set(_lib_dir "${CMAKE_BINARY_DIR}/lib")
  endif ()

  file(RELATIVE_PATH _rel_lib "${_ptests_dir}" "${_lib_dir}")
  string(REPLACE "\\" "/" _rel_lib "${_rel_lib}")

  get_target_property(_cur_rpath ${PT_TARGET} BUILD_RPATH)
  if (NOT _cur_rpath)
    set(_cur_rpath "")
  endif ()

  set_target_properties(
    ${PT_TARGET}
    PROPERTIES BUILD_RPATH "${_cur_rpath};\$ORIGIN/${_rel_lib};${_lib_dir}"
               SKIP_BUILD_RPATH OFF
               BUILD_WITH_INSTALL_RPATH OFF
  )

  set_property(
    TARGET ${PT_TARGET}
    APPEND_STRING
    PROPERTY LINK_FLAGS " -Wl,-rpath-link,${_lib_dir}"
  )

  # NOTE: No gtest_add_tests() - ptests are not registered with CTest

  # Summary
  set(_gperf "off")
  if (GPERF_PROFILER_LIB OR GPERF_TCMALLOC_LIB)
    set(_gperf "on")
  endif ()
  set(_bench "off")
  if (TARGET vernier::bench)
    set(_bench "on")
  endif ()
  message(
    STATUS
      "[Perf] target=${PT_TARGET} out=ptests/ benchlib=${_bench} gperftools=${_gperf} (manual execution only)"
  )
endfunction ()

# ------------------------------------------------------------------------------
# apex_add_devtest(...)  -- COMPONENT-LEVEL test (dtests/), NOT registered with CTest
#
# Add a component-level test executable that is built but NOT registered with
# CTest. These exercise a whole component (or its integration with others) and
# are too coarse or slow for the unit gate, so they run manually.
#
# The executable is placed in bin/dtests/ and can be run manually with:
#   ./build/hosted-x86_64-debug/bin/dtests/TestName --gtest_filter="*Pattern*"
#
# Arguments:
#   TARGET          <n>              required
#   SOURCES         <src...>         required
#   CUDA            <cu...>          optional
#   LINK            <libs...>        optional
#   INC             <dir>            optional
# ------------------------------------------------------------------------------
function (apex_add_devtest)
  # Skip on bare-metal
  if (APEX_PLATFORM_BAREMETAL)
    return()
  endif ()

  cmake_parse_arguments(DT "" "TARGET;INC" "SOURCES;CUDA;LINK" ${ARGN})
  apex_require(DT_TARGET DT_SOURCES)

  # Dev-test config: bin/dtests/, plain (no PCH/coverage/CTest).
  _apex_test_executable(
    TARGET
    ${DT_TARGET}
    OUTDIR
    dtests
    SOURCES
    ${DT_SOURCES}
    CUDA
    ${DT_CUDA}
    LINK
    ${DT_LINK}
    INC
    "${DT_INC}"
  )

  # NOTE: No gtest_add_tests() - dev tests are not registered with CTest

  message(STATUS "[Dev] target=${DT_TARGET} out=dtests/ (manual execution only)")
endfunction ()
