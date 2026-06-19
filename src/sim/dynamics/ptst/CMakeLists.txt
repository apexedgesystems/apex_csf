# ==============================================================================
# Performance Tests: sim_dynamics
# ==============================================================================

include(apex/All)

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Dynamics_pTest.cpp")

apex_add_ptest(
  TARGET
  SimDynamics_PTEST
  SOURCES
  ${PERF_TEST_SOURCES}
  LINK
  sim_dynamics_integrators
  sim_dynamics_rigid_body
  sim_dynamics_mass_properties
  sim_dynamics_disturbance
)
