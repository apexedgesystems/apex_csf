# ==============================================================================
# Unit Tests: sim_analog_regulator
#
# Unit tests for the voltage regulator model.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/RegulatorModel_uTest.cpp")

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSimAnalogRegulator
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_analog_regulator
  LABELS
  sim
  analog
)
