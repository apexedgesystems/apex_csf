# ==============================================================================
# Performance Tests: system_core_protocols_framing_cobs
#
# COBS encode/decode throughput, payload-size sweeps, and cache-hierarchy probes.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/COBSFraming_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET COBSFraming_PTEST SOURCES ${PERF_TEST_SOURCES} LINK system_core_protocols_framing_cobs
)
