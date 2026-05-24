# ==============================================================================
# Performance Tests: utilities_checksums_crc
#
# Throughput, latency, and hardware acceleration benchmarks for CRC variants.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Crc_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(TARGET Crc_PTEST SOURCES ${PERF_TEST_SOURCES} LINK utilities_checksums_crc)
