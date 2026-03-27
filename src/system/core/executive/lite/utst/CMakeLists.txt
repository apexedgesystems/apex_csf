# ==============================================================================
# executive_lite - Unit Tests
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (3 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/ITickSource_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/FreeRunningSource_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LiteExecutive_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestExecutiveLite
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  executive_lite
  LABELS
  system_core
)
