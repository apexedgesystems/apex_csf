# ==============================================================================
# Unit Tests: utilities_checksums_crc
#
# Canonical check value tests for all CRC implementations.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (3 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/CrcTable_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CrcBitwise_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CrcNibble_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestChecksumsCrc
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  utilities_checksums_crc
  fmt::fmt
  LABELS
  checksums
  crc
)
