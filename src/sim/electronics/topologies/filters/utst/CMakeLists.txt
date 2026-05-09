# ==============================================================================
# Unit Tests: sim_electronics_topologies_filters
#
# Validation against analytical step response and magnitude response.
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/RcLowPass_uTest.cpp")

apex_add_gtest(
  TARGET
  TestSimElectronicsFilters
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_electronics_topologies_filters
  sim_electronics_circuit
  sim_electronics_devices_linear
  sim_electronics_algorithms_transient
  COVERAGE_FOR
  sim_electronics_topologies_filters
  LABELS
  sim
  electronics
  analog
)
