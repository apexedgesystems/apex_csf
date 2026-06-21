# ==============================================================================
# Performance Tests: sim_dynamics_integrators
#
# Benchmarks the RK4 integrator kernel (four-stage step over a cheap analytic
# derivative, so the cost measured is the integrator machinery).
# ==============================================================================

include(apex/All)

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Integrators_pTest.cpp")

apex_add_ptest(
  TARGET SimDynamicsIntegrators_PTEST SOURCES ${PERF_TEST_SOURCES} LINK sim_dynamics_integrators
)
