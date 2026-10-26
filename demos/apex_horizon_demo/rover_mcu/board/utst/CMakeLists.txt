include(apex/All)

apex_add_gtest(
  TARGET
  TestApexHorizonDemoRoverBoard
  SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/RoverBoardController_uTest.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/RoverBoardDelayedFeedback_uTest.cpp"
  LINK
  apex_horizon_demo_rover_board
  apex_horizon_demo_rover_controller
  apex_horizon_demo_ground_vehicle
  sim_environment_celestial_body
  LABELS
  apex_horizon_demo
  rover_mcu
)
