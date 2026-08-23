# ==============================================================================
# Unit Tests: apex_horizon_demo_aircraft_controller
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/AircraftControllerData_uTest.cpp"
                      "${CMAKE_CURRENT_SOURCE_DIR}/AircraftControllerClosedLoop_uTest.cpp"
)

apex_add_gtest(
  TARGET
  TestApexHorizonDemoAircraftController
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  apex_horizon_demo_aircraft_controller
  LABELS
  apex_horizon_demo
  aircraft_controller
)
