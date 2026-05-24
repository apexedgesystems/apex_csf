# ==============================================================================
# Performance Tests: system_core_protocols_framing_slip
#
# SLIP encode/decode throughput, payload scaling, cache hierarchy, and config.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/SLIPFraming_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET SLIPFraming_PTEST SOURCES ${PERF_TEST_SOURCES} LINK system_core_protocols_framing_slip
)
