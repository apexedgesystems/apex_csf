# ==============================================================================
# Performance Tests: system_core_protocols_network_tcp
#
# TCP echo round-trip latency, throughput, multi-client, and payload scaling.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/TcpSocket_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET TcpSocket_PTEST SOURCES ${PERF_TEST_SOURCES} LINK system_core_protocols_network_tcp
)
