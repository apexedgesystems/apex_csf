# ==============================================================================
# hal_pico Unit Tests (mock mode)
# ==============================================================================

# Skip tests for bare-metal builds
if (APEX_PLATFORM_BAREMETAL)
  return()
endif ()

# ------------------------------------------------------------------------------
# Sources (4 test files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/PicoFlash_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PicoSysTickSource_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PicoUart_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PicoUsbCdc_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Executable
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestHalPico
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  hal_interface
  executive_mcu
  LABELS
  hal
)

# Enable mock mode for Pico tests on host
target_compile_definitions(TestHalPico PRIVATE APEX_HAL_PICO_MOCK)
