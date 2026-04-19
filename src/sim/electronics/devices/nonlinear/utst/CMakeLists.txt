# ==============================================================================
# Unit Tests: sim_electronics_devices_nonlinear
# ==============================================================================

set(TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/BjtEbersMoll_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/DiodeShockley_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/DiodeSpice_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/JfetLevel2_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/JfetShichman_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MosfetBinarySwitch_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MosfetLevel1_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MosfetLevel2_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MosfetLevel3_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SchottkyDiode_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ZenerDiode_uTest.cpp"
)

apex_add_gtest(
  TARGET
  sim_electronics_devices_nonlinear_uTest
  SOURCES
  ${TEST_SOURCES}
  LINK
  sim_electronics_devices_nonlinear
  COVERAGE_FOR
  sim_electronics_devices_nonlinear
  LABELS
  devices
)
