# ==============================================================================
# Tests: sim_environment_atmosphere
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Test Sources
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/AtmosphereStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Atm_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ConstantAtmosphere_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ExponentialAtmosphere_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LayeredAtmosphere_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Ussa76AtmosphereModel_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/VacuumAtmosphereModel_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSimEnvironmentAtmosphere
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_environment_atmosphere
  fmt::fmt
  COVERAGE_FOR
  sim_environment_atmosphere
  LABELS
  sim
  environment
  atmosphere
)
