# ==============================================================================
# Unit Tests: system_core_protocols_wireless_bluetooth
#
# Tests Bluetooth RFCOMM adapter using socketpair loopback including:
#   - RfcommAdapter FD injection, read/write, ByteTrace, stats, disconnect
#   - RfcommConfig BluetoothAddress parsing/formatting, validation
#   - RfcommLoopback open/close, server read/write, FD release
#   - RfcommStats counter fields and reset
#   - RfcommStatus toString coverage
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/RfcommStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/RfcommStats_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/RfcommConfig_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/RfcommLoopback_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/RfcommAdapter_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreProtocolsWirelessBluetooth
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_protocols_wireless_bluetooth
  fmt::fmt
  LABELS
  system
  protocols
  wireless
  bluetooth
)
