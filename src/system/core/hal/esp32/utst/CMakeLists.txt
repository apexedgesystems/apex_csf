# ==============================================================================
# hal_esp32 Unit Tests (mock mode)
# ==============================================================================

# Skip tests for bare-metal builds
if (APEX_PLATFORM_BAREMETAL)
  return()
endif ()

# ------------------------------------------------------------------------------
# Sources (4 test files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Esp32NvsFlash_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Esp32TimerTickSource_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Esp32Uart_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Esp32UsbCdc_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Executable
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestHalEsp32
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  hal_interface
  executive_lite
  LABELS
  hal
)

# Enable mock mode for ESP32 tests on host
target_compile_definitions(TestHalEsp32 PRIVATE APEX_HAL_ESP32_MOCK)
