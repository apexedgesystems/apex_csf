# ==============================================================================
# Unit Tests: sim_dynamics_integrators
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Integrators_uTest.cpp")

apex_add_gtest(
  TARGET
  TestSimDynamicsIntegrators
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_dynamics_integrators
  LABELS
  sim
  dynamics
)
