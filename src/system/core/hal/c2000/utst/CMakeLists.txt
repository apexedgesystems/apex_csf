# ==============================================================================
# Unit Tests: hal_c2000
#
# Mock-mode tests for C2000 HAL (host-side, no driverlib).
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (2 test files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/C2000Uart_uTest.cpp"
                      "${CMAKE_CURRENT_SOURCE_DIR}/C2000TimerTickSource_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestHalC2000
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  hal_c2000
  LABELS
  hal
  c2000
)
