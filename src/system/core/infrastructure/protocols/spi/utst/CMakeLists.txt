# ==============================================================================
# Unit Tests: system_core_protocols_spi
#
# Tests SPI status, configuration, adapter, and socket device implementations.
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources (4 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/SpiStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SpiConfig_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SpiAdapter_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SpiSocketDevice_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreProtocolsSpi
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_protocols_spi
  fmt::fmt
  LABELS
  system
  protocols
  spi
)
