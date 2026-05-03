# ==============================================================================
# hal_interface Unit Tests
# ==============================================================================

# Skip tests for bare-metal builds
if (APEX_PLATFORM_BAREMETAL)
  return()
endif ()

# ------------------------------------------------------------------------------
# Sources (6 test files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/ICan_uTest.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/IFlash_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/II2c_uTest.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/IPps_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ISpi_uTest.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/IUart_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Executable
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestHalInterface
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  hal_interface
  LABELS
  hal
)
