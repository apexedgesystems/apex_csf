include(apex/All)

apex_add_gtest(
  TARGET
  TestApexHorizonDemoRoverLidarModel
  SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/RoverLidarModel_uTest.cpp"
  LINK
  apex_horizon_demo_rover_lidar_model
  LABELS
  apex_horizon_demo
  rover_mcu
)
