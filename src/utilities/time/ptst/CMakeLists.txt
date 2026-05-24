# ==============================================================================
# Performance Tests: utilities_time
#
# Clock read latency and time conversion throughput benchmarks.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Time_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(TARGET Time_PTEST SOURCES ${PERF_TEST_SOURCES} LINK utilities_time)
