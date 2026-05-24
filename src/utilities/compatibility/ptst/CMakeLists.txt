# ==============================================================================
# Performance Tests: utilities_compatibility
#
# Byteswap throughput benchmarks across integer and IEEE-754 types.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Compatibility_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(TARGET Compatibility_PTEST SOURCES ${PERF_TEST_SOURCES} LINK utilities_compatibility)
