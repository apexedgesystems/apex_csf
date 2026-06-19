# ==============================================================================
# Performance Tests: sim_environment_atmosphere
#
# Benchmarks the atmosphere models: layered (USSA76) hydrostatic query
# throughput, exponential analytic query throughput, and `.atm` file load time
# (header + body read + layer table build).
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Atmosphere_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET
  SimEnvironmentAtmosphere_PTEST
  SOURCES
  ${PERF_TEST_SOURCES}
  LINK
  sim_environment_atmosphere
  fmt::fmt
)
