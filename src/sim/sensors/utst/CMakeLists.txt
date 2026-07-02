# ==============================================================================
# Unit Tests: sim_sensors
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/GaussianSampler_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Sensors_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/BoxClearanceLidar_uTest.cpp"
)

apex_add_gtest(
  TARGET
  TestSimSensors
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_sensors
  LABELS
  sim
  sensors
)
