# ==============================================================================
# Development Tests: system_core_protocols_fieldbus_modbus
# ==============================================================================

# ------------------------------------------------------------------------------
# Dev Test Sources
# ------------------------------------------------------------------------------

set(DEV_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ModbusEmulation_dTest.cpp")

# ------------------------------------------------------------------------------
# Development Tests (manual execution only)
# ------------------------------------------------------------------------------

apex_add_devtest(
  TARGET ModbusEmulation_Dev SOURCES ${DEV_TEST_SOURCES} LINK system_core_protocols_fieldbus_modbus
)
