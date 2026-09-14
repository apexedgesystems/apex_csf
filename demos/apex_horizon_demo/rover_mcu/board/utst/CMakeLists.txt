include(apex/All)

apex_add_gtest(
  TARGET
  TestApexHorizonDemoRoverBoard
  SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/RoverBoardController_uTest.cpp"
  LINK
  apex_horizon_demo_rover_board
  LABELS
  apex_horizon_demo
  rover_mcu
)
