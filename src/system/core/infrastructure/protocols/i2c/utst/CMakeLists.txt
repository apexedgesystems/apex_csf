# ==============================================================================
# Unit Tests: system_core_protocols_i2c
#
# Tests I2C status codes, configuration, adapter lifecycle, and socketpair
# transport.
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources (4 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/I2cStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/I2cConfig_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/I2cAdapter_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/I2cSocketDevice_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreProtocolsI2c
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_protocols_i2c
  fmt::fmt
  LABELS
  system
  protocols
  i2c
)
