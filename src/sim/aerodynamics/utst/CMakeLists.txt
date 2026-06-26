# ==============================================================================
# Unit Tests: sim_aerodynamics
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/PolarAero_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/AeroGolden_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/StabilityDerivativeAero_uTest.cpp"
)

apex_add_gtest(
  TARGET
  TestSimAerodynamics
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_aerodynamics
  LABELS
  sim
  aerodynamics
)
