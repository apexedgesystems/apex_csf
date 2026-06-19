# ==============================================================================
# Tests: sim_environment_celestial_body
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/CelestialBody_uTest.cpp")

apex_add_gtest(
  TARGET
  TestSimEnvironmentCelestialBody
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_environment_celestial_body
  fmt::fmt
  COVERAGE_FOR
  sim_environment_celestial_body
  LABELS
  sim
  environment
  celestial_body
)
