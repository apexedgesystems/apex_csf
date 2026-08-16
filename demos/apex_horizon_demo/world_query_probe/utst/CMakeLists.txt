# ==============================================================================
# Unit tests: WorldQueryProbe demo component
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/WorldQueryProbe_uTest.cpp")

apex_add_gtest(
  TARGET
  TestWorldQueryProbe
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  apex_horizon_demo_world_query_probe
  sim_environment_celestial_body
  LABELS
  demos
  horizon
)
