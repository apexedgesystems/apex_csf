# ==============================================================================
# Unit Tests: system_core_protocols_fieldbus_lin
#
# Tests LIN status codes, frame building/parsing, configuration, and controller
# master/slave operations via PTY loopback.
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources (4 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/LinStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LinFrame_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LinConfig_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LinController_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreProtocolsFieldbusLin
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_protocols_fieldbus_lin
  fmt::fmt
  LABELS
  system
  protocols
  fieldbus
  lin
)
