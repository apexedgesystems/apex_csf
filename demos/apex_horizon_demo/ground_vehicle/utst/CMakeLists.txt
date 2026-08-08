# ==============================================================================
# Unit tests: GroundVehicle demo component
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/GroundVehicle_uTest.cpp")

apex_add_gtest(
  TARGET
  TestGroundVehicle
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  apex_horizon_demo_ground_vehicle
  sim_environment_celestial_body
  LABELS
  demos
  horizon
)
