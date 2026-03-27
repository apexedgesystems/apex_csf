# ==============================================================================
# Unit Tests: TCP Socket
#
# Tests TCP socket server/client operations including:
#   - Server init and socket lifecycle
#   - Single-client echo round-trip
#   - Multi-client concurrent connections
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/TcpSocket_uTest.cpp")

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreProtocolsNetworkTcp
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_protocols_network_tcp
  fmt::fmt
  LABELS
  system
  protocols
  network
  tcp
  RESOURCE_LOCK
  network_tcp_ports
  REQUIRES_THREADS
  2
)
