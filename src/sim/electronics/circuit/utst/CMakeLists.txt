# ==============================================================================
# Unit Tests: sim_electronics_circuit
#
# Tests for the circuit construction and simulation API: net allocation,
# stamp registration, companion management, build lifecycle, and transient
# simulation via a simple RC voltage divider.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Circuit_uTest.cpp")

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSimElectronicsCircuit
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_electronics_circuit
  sim_electronics_devices_linear
  COVERAGE_FOR
  sim_electronics_circuit
  LABELS
  sim
  electronics
  circuit
)
