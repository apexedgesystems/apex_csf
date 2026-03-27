# ==============================================================================
# Development Tests: system_core_protocols_serial_uart
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(DEV_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/UartEmulation_dTest.cpp")

# ------------------------------------------------------------------------------
# Dev Test Target (manual execution only)
# ------------------------------------------------------------------------------

apex_add_devtest(
  TARGET UartEmulation_Dev SOURCES ${DEV_TEST_SOURCES} LINK system_core_protocols_serial_uart
)
