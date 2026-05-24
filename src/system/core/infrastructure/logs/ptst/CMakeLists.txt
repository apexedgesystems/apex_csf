# ==============================================================================
# Performance Tests: system_core_logs
#
# SYNC vs ASYNC log path throughput, skip-path latency, and contention scaling.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/SystemLog_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(TARGET SystemLog_PTEST SOURCES ${PERF_TEST_SOURCES} LINK system_core_logs)
