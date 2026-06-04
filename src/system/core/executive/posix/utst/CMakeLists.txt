# ==============================================================================
# Unit Tests: system_core_executive
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Test Sources (4)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/ExecutiveStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/RTMode_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ExecutiveData_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ShutdownConfig_uTest.cpp"
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
  fmt::fmt
  LABELS
  system
  core
  executive
)
