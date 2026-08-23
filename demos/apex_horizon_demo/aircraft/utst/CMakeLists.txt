# ==============================================================================
# Unit Tests: apex_horizon_demo_aircraft
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/AircraftData_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/AircraftCommand_uTest.cpp" # handleCommand surface
    "${CMAKE_CURRENT_SOURCE_DIR}/AircraftWire_uTest.cpp"
)

apex_add_gtest(
  TARGET
  TestApexHorizonDemoAircraft
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  apex_horizon_demo_aircraft
  LABELS
  apex_horizon_demo
  aircraft
)
