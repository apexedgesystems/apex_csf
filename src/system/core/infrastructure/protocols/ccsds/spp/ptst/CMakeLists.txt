# ==============================================================================
# Performance Tests: system_core_protocols_ccsds_spp
#
# SPP packing, viewing, and streaming processor extraction throughput.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/CcsdsSpp_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET CcsdsSpp_PTEST SOURCES ${PERF_TEST_SOURCES} LINK system_core_protocols_ccsds_spp
)
