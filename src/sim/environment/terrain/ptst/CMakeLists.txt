# ==============================================================================
# Performance Tests: sim_environment_terrain
#
# Benchmarks the `.htile` terrain consumer: load time (header + body read) and
# elevationAt query throughput over a synthetic in-memory / temp tile.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/HtileTile_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET
  SimEnvironmentTerrain_PTEST
  SOURCES
  ${PERF_TEST_SOURCES}
  LINK
  sim_environment_terrain
  fmt::fmt
)
