# ==============================================================================
# Unit Tests: sim_electronics_topologies_amplifiers
#
# DC operating point validation for BJT common-emitter amplifier across
# active, saturation, and cutoff regions.
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/BjtCommonEmitter_uTest.cpp")

apex_add_gtest(
  TARGET
  TestSimElectronicsAmplifiers
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_electronics_topologies_amplifiers
  sim_electronics_circuit
  sim_electronics_devices_nonlinear
  sim_electronics_algorithms_transient
  COVERAGE_FOR
  sim_electronics_topologies_amplifiers
  LABELS
  sim
  electronics
  analog
)
