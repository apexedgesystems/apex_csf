# ==============================================================================
# Unit Tests: sim_dynamics_wrench
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Wrench_uTest.cpp")

apex_add_gtest(
  TARGET
  TestSimDynamicsWrench
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_dynamics_wrench
  LABELS
  sim
  dynamics
  wrench
)
