# ==============================================================================
# Performance Tests: system_core_schedulable
#
# Task dispatch latency, binding overhead, cache effects, and scaling.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/SchedulableTask_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET
  SchedulableTask_PTEST
  SOURCES
  ${PERF_TEST_SOURCES}
  LINK
  system_core_schedulable
  utilities_concurrency
)
