# ==============================================================================
# Tests: sim_environment_world
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Test Sources
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/WorldBundle_uTest.cpp")

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSimEnvironmentWorld
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_environment_world
  fmt::fmt
  COVERAGE_FOR
  sim_environment_world
  LABELS
  sim
  environment
  world
)
