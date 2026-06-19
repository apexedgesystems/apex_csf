# ==============================================================================
# Tests: sim_environment_terrain
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Test Sources
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/TerrainStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Htile_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/HtileTile_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ConstantTerrain_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SphereTerrain_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/EllipsoidTerrain_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SrtmTerrainModel_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LolaTerrainModel_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSimEnvironmentTerrain
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_environment_terrain
  fmt::fmt
  COVERAGE_FOR
  sim_environment_terrain
  LABELS
  sim
  environment
  terrain
)
