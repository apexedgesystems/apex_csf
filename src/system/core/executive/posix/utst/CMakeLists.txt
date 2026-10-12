# ==============================================================================
# Unit Tests: system_core_executive
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Test Sources (7)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/ExecutiveStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/RTMode_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ExecutiveData_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ShutdownConfig_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TickLateness_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ClockGrid_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ExecutiveCli_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreExecutive
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_executive
  # The executive embeds these by value in its header, so a test that
  # constructs one links them directly (the library keeps them private).
  system_core_scheduler
  system_core_filesystem
  fmt::fmt
  LABELS
  system
  core
  executive
)
