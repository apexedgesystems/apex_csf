# ==============================================================================
# Performance Tests: system_core_registry
#
# Component / task / data lookup latency, bulk iteration, and registration
# overhead benchmarks for the Apex registry.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Registry_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET
  Registry_PTEST
  SOURCES
  ${PERF_TEST_SOURCES}
  LINK
  system_core_registry
  system_core_schedulable
  fmt::fmt
)
