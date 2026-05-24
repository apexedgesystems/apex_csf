# ==============================================================================
# Performance Tests: utilities_helpers
#
# Throughput benchmarks for byte operations, hex encoding, and bit manipulation.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Helpers_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(TARGET Helpers_PTEST SOURCES ${PERF_TEST_SOURCES} LINK utilities_helpers)
