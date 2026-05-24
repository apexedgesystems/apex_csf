# ==============================================================================
# Performance Tests: system_core_action
#
# Action engine processCycle pipeline throughput, per-stage latency, full-scale
# table evaluation, and catalog lookup performance.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ActionInterface_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET
  ActionInterface_PTEST
  SOURCES
  ${PERF_TEST_SOURCES}
  LINK
  system_core_action
  fmt::fmt
)
