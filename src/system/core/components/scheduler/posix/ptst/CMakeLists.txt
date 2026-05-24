# ==============================================================================
# Performance Tests: system_core_scheduler
#
# Single-thread tick overhead, multi-thread dispatch / scaling, and intra-frame
# sequencing overhead benchmarks for the POSIX scheduler.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (3 files)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES_ST "${CMAKE_CURRENT_SOURCE_DIR}/SchedulerSingleThread_pTest.cpp")
set(PERF_TEST_SOURCES_MT "${CMAKE_CURRENT_SOURCE_DIR}/SchedulerMultiThread_pTest.cpp")
set(PERF_TEST_SOURCES_SEQ "${CMAKE_CURRENT_SOURCE_DIR}/SequencingOverhead_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET
  SchedulerSingleThread_PTEST
  SOURCES
  ${PERF_TEST_SOURCES_ST}
  LINK
  system_core_scheduler
  system_core_logs
  utilities_concurrency
  fmt::fmt
)

apex_add_ptest(
  TARGET
  SchedulerMultiThread_PTEST
  SOURCES
  ${PERF_TEST_SOURCES_MT}
  LINK
  system_core_scheduler
  system_core_logs
  utilities_concurrency
  fmt::fmt
)

apex_add_ptest(
  TARGET
  SequencingOverhead_PTEST
  SOURCES
  ${PERF_TEST_SOURCES_SEQ}
  LINK
  system_core_scheduler
  system_core_logs
  utilities_concurrency
  utilities_helpers
  fmt::fmt
)
