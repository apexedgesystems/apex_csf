# ==============================================================================
# executive_mcu - Unit Tests
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (3 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/ITickSource_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/FreeRunningSource_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/McuExecutive_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestExecutiveMcu
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  executive_mcu
  LABELS
  system_core
)
