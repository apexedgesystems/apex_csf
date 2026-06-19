# ==============================================================================
# Module: sim_environment_atmosphere (dtst)
#
# Development tests for integration and validation. These tests require external
# data files (horizon-generated .atm fixtures) and are NOT registered with
# CTest -- run manually:
#   ./build/native-linux-debug/bin/devtests/SimEnvironmentAtmosphere_Dev
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(DEV_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/00_HorizonProceduralAtm_dTest.cpp"
                     "${CMAKE_CURRENT_SOURCE_DIR}/01_HorizonRealUssa76_dTest.cpp"
)

# ------------------------------------------------------------------------------
# Development Tests (manual execution only)
# ------------------------------------------------------------------------------

apex_add_devtest(
  TARGET
  SimEnvironmentAtmosphere_Dev
  SOURCES
  ${DEV_TEST_SOURCES}
  LINK
  sim_environment_atmosphere
  fmt::fmt
)
