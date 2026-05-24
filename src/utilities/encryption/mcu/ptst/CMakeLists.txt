# ==============================================================================
# Performance Tests: encryption_mcu
#
# AES-256-GCM software encryption throughput benchmarks.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/EncryptionMcu_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(TARGET EncryptionMcu_PTEST SOURCES ${PERF_TEST_SOURCES} LINK encryption_mcu)
