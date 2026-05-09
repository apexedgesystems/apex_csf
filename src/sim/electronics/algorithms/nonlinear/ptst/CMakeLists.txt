# ==============================================================================
# Performance Tests: sim_electronics_algorithms_nonlinear
#
# NR iteration throughput and convergence for small nonlinear circuits.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/AlgorithmsNonlinear_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET AlgorithmsNonlinear_PTEST SOURCES ${PERF_TEST_SOURCES} LINK
  sim_electronics_algorithms_nonlinear
)
