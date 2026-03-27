# ==============================================================================
# Unit Tests: system_core_action
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Test Sources (1)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ActionComponent_uTest.cpp")

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreAction
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_action
  fmt::fmt
  LABELS
  system
  core
  action
)
