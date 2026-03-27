# ==============================================================================
# Unit Tests: system_core_protocols_fieldbus_modbus
#
# Tests Modbus status, exception codes, types, statistics, configuration,
# frame building/parsing, RTU/TCP transports, and master operations.
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources (9 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/ModbusStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ModbusException_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ModbusTypes_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ModbusStats_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ModbusConfig_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ModbusFrame_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ModbusRtuTransport_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ModbusTcpTransport_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ModbusMaster_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreProtocolsFieldbusModbus
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_protocols_fieldbus_modbus
  fmt::fmt
  LABELS
  system
  protocols
  fieldbus
  modbus
)
