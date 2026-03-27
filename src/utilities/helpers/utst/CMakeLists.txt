# ==============================================================================
# Unit Tests: utilities_helpers
#
# Unit tests for helper utilities.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (14 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/parseArgs_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/getEndianness_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/runningAverage_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/checkFileExists_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/containsString_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/printSpan_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/toBytes_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/hex2struct_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/BitOps_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ByteOps_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Cpu_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Files_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Format_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Strings_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestUtilitiesHelpers
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  utilities_helpers
  fmt::fmt
  LABELS
  utilities
  helpers
)
