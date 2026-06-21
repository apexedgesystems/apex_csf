# ==============================================================================
# Unit Tests: sim_dynamics_vehicle
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/VehicleStep_uTest.cpp")

apex_add_gtest(
  TARGET
  TestSimDynamicsVehicle
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_dynamics_vehicle
  LABELS
  sim
  dynamics
)
