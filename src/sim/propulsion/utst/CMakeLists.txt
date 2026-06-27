# ==============================================================================
# Unit Tests: sim_propulsion
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/DensityScaledThrust_uTest.cpp"
                      "${CMAKE_CURRENT_SOURCE_DIR}/Turbofan2Spool_uTest.cpp"
)

apex_add_gtest(
  TARGET
  TestSimPropulsion
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_propulsion
  LABELS
  sim
  propulsion
)
