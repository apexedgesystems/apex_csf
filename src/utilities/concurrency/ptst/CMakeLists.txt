# ==============================================================================
# Performance Tests: utilities_concurrency
#
# Throughput and latency benchmarks for queues, locks, semaphores, and pools.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Concurrency_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(TARGET Concurrency_PTEST SOURCES ${PERF_TEST_SOURCES} LINK utilities_concurrency)
