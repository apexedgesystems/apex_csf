# ==============================================================================
# Unit Tests: system_core_time_server
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/TimeServerData_uTest.cpp")

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreTimeServer
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_time_server
  LABELS
  system
  core
  time_server
)
