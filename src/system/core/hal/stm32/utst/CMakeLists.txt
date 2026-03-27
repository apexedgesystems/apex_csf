# ==============================================================================
# hal_stm32 Unit Tests (mock mode)
# ==============================================================================

# Skip tests for bare-metal builds
if (APEX_PLATFORM_BAREMETAL)
  return()
endif ()

# ------------------------------------------------------------------------------
# Sources (8 test files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/FreeRtosTickSource_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Stm32Can_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Stm32Flash_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Stm32I2c_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Stm32Spi_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Stm32SysTickSource_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Stm32TimerTickSource_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Stm32Uart_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Executable
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestHalStm32
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  hal_interface
  executive_lite
  LABELS
  hal
)

# Enable mock mode for STM32 tests on host
target_compile_definitions(TestHalStm32 PRIVATE APEX_HAL_STM32_MOCK)
