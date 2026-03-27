# ==============================================================================
# Unit Tests: UART Serial
#
# Tests UART adapter and supporting types including:
#   - Status enum values and toString() conversion
#   - Statistics struct (reset, totalBytes, totalErrors, totalOperations)
#   - Configuration enums (BaudRate, DataBits, Parity, StopBits, FlowControl)
#   - PTY pair lifecycle (open, close, move semantics, master I/O)
#   - Adapter configure, read, write, flush, vectored I/O, span API
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources (5 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/UartStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UartStats_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UartConfig_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PtyPair_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UartAdapter_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreProtocolsSerialUart
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_protocols_serial_uart
  LABELS
  system
  protocols
  serial
  uart
)
