# ==============================================================================
# Performance Tests: utilities_data_proxy
#
# Throughput benchmarks for byte mask, endianness, and master data proxy paths.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/DataProxy_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(TARGET DataProxy_PTEST SOURCES ${PERF_TEST_SOURCES} LINK utilities_data_proxy)
