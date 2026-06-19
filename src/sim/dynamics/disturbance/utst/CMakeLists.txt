# ==============================================================================
# Unit Tests: sim_dynamics_disturbance
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/DrydenTurbulence_uTest.cpp")

apex_add_gtest(
  TARGET
  TestSimDynamicsDisturbance
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_dynamics_disturbance
  LABELS
  sim
  dynamics
  disturbance
)
