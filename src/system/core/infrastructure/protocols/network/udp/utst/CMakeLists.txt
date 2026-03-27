# ==============================================================================
# Unit Tests: UDP Socket
#
# Tests UDP socket server/client operations including:
#   - Connected and unconnected mode echo
#   - Multi-client concurrent datagrams
#   - Connection statistics tracking
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/UdpSocket_uTest.cpp")

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreProtocolsNetworkUdp
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_protocols_network_udp
  fmt::fmt
  LABELS
  system
  protocols
  network
  udp
  RESOURCE_LOCK
  network_udp_ports
  REQUIRES_THREADS
  2
)
