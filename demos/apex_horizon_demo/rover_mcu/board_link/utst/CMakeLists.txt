include(apex/All)

apex_add_gtest(
  TARGET
  TestApexHorizonDemoRoverBoardLink
  SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/RoverBoardLink_uTest.cpp"
  LINK
  apex_horizon_demo_rover_board_link
  LABELS
  apex_horizon_demo
  rover_mcu
)
