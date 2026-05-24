# ==============================================================================
# Performance Tests: system_core_protocols_fieldbus_modbus
#
# Modbus RTU frame build/parse, CRC-16, and PTY-loopback transaction latency.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ModbusRtu_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET ModbusRtu_PTEST SOURCES ${PERF_TEST_SOURCES} LINK system_core_protocols_fieldbus_modbus
)
