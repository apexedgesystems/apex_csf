# ==============================================================================
# Unit Tests: sim_dynamics_force_moment
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ForceMoment_uTest.cpp")

apex_add_gtest(
  TARGET
  TestSimDynamicsForceMoment
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_dynamics_force_moment
  LABELS
  sim
  dynamics
  force_moment
)
