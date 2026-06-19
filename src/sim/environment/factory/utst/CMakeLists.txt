# ==============================================================================
# Tests: sim_environment_factory
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/EnvironmentFactory_uTest.cpp")

apex_add_gtest(
  TARGET
  TestSimEnvironmentFactory
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_environment_factory
  fmt::fmt
  COVERAGE_FOR
  sim_environment_factory
  LABELS
  sim
  environment
  factory
)
