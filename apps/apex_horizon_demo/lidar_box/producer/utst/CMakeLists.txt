# ==============================================================================
# Unit Tests: apex_horizon_demo_lidar_box_producer
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/LidarBoxProducer_uTest.cpp")

apex_add_gtest(
  TARGET
  TestLidarBoxProducer
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  apex_horizon_demo_lidar_box_producer
  LABELS
  apps
  lidar_box
)
