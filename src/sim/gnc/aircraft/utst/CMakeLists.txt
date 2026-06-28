# ==============================================================================
# Unit Tests: sim_gnc_aircraft
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/LongitudinalControllers_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LateralControllers_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/GustAlleviation_uTest.cpp"
)

apex_add_gtest(
  TARGET
  TestSimGncAircraft
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_gnc_aircraft
  LABELS
  sim
  gnc
  aircraft
)
