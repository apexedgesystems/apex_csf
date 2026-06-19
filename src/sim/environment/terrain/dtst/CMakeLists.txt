# ==============================================================================
# Module: sim_environment_terrain (dtst)
#
# Development tests for integration and validation. These tests require external
# data files (horizon-generated .htile fixtures) and are NOT registered with
# CTest -- run manually:
#   ./build/native-linux-debug/bin/devtests/SimEnvironmentTerrain_Dev
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(DEV_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/00_HorizonProceduralHtile_dTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/02_HorizonRealSrtm_dTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/03_HorizonRealLola_dTest.cpp"
)

# ------------------------------------------------------------------------------
# Development Tests (manual execution only)
# ------------------------------------------------------------------------------

apex_add_devtest(
  TARGET
  SimEnvironmentTerrain_Dev
  SOURCES
  ${DEV_TEST_SOURCES}
  LINK
  sim_environment_terrain
  fmt::fmt
)
