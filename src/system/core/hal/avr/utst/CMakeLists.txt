# ==============================================================================
# hal_avr Unit Tests (mock mode)
# ==============================================================================

# Skip tests for bare-metal builds
if (APEX_PLATFORM_BAREMETAL)
  return()
endif ()

# ------------------------------------------------------------------------------
# Sources (3 test files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/AvrEeprom_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/AvrTimerTickSource_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/AvrUart_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Executable
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestHalAvr
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  hal_interface
  executive_lite
  LABELS
  hal
)

# Enable mock mode for AVR tests on host
target_compile_definitions(TestHalAvr PRIVATE APEX_HAL_AVR_MOCK)
