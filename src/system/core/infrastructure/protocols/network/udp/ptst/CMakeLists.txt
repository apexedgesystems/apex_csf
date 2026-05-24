# ==============================================================================
# Performance Tests: system_core_protocols_network_udp
#
# UDP echo round-trip latency, send throughput, mode comparison, and scaling.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/UdpSocket_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET UdpSocket_PTEST SOURCES ${PERF_TEST_SOURCES} LINK system_core_protocols_network_udp
)
