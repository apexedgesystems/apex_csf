# ==============================================================================
# Unit Tests: apex_horizon_demo_rover_controller
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/RoverControllerClosedLoop_uTest.cpp")

apex_add_gtest(
  TARGET
  TestApexHorizonDemoRoverController
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  apex_horizon_demo_rover_controller
  LABELS
  apex_horizon_demo
  rover_controller
)
