include(apex/All)

apex_add_gtest(
  TARGET
  TestApexHorizonDemoRoverActionOrdering
  SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/RoverActionOrdering_uTest.cpp"
  LINK
  apex_horizon_demo_ground_vehicle
  sim_environment_celestial_body
  system_core_scheduler
  system_core_logs
  utilities_concurrency
  fmt::fmt
  LABELS
  apex_horizon_demo
  rover_mcu
)
