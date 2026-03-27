# ==============================================================================
# Unit Tests: Unix Domain Socket
#
# Tests Unix domain socket server/client operations including:
#   - Server init and socket lifecycle
#   - Single-client echo round-trip
#   - Multi-client concurrent connections
#   - Connection statistics and limits
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/UnixSocket_uTest.cpp")

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreProtocolsNetworkUnix
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_protocols_network_unix
  fmt::fmt
  LABELS
  system
  protocols
  network
  unix
)
