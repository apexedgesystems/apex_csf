# ==============================================================================
# Unit Tests: system_core_protocols_aproto
#
# Tests APROTO encoding and decoding operations including:
#   - Header structure and field sizes
#   - Encode/decode round-trip
#   - CRC validation
#   - Status code coverage
#   - Error handling
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources (5 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/AprotoTypes_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/AprotoStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/AprotoCodec_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/AprotoProcessor_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/AprotoMutableMessage_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreProtocolsAproto
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_protocols_aproto
  LABELS
  system
  protocols
  aproto
)
