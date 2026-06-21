# ==============================================================================
# Performance Tests: sim_dynamics_disturbance
#
# Benchmarks the Dryden turbulence step (3-axis PSD-shaped filter plus the
# three Gaussian RNG draws, which dominate the cost).
# ==============================================================================

include(apex/All)

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Disturbance_pTest.cpp")

apex_add_ptest(
  TARGET SimDynamicsDisturbance_PTEST SOURCES ${PERF_TEST_SOURCES} LINK sim_dynamics_disturbance
)
