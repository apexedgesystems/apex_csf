# ==============================================================================
# Unit Tests: system_core_monte_carlo
#
# Unit tests for Monte Carlo batch execution infrastructure.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (5 files, plus the concurrency tests' thread-launch seam)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/MonteCarloDriver_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MonteCarloResults_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SweepGenerator_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MonteCarloExport_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ConvergenceTracker_uTest.cpp"
    "${CMAKE_SOURCE_DIR}/src/utilities/concurrency/utst/support/ThreadStartFault.cpp"
)

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreMonteCarlo
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_monte_carlo
  ${CMAKE_DL_LIBS}
  LABELS
  system
  monte_carlo
)
