# ==============================================================================
# Unit Tests: sim_electronics_intel4004_gate
#
# Fast gate-level verification: extraction, NOR truth tables, propagation.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/GateLevel_uTest.cpp")

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSimElectronicsIntel4004Gate
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_electronics_intel4004_gate
  sim_electronics_intel4004_netlist
  COVERAGE_FOR
  sim_electronics_intel4004_gate
  LABELS
  sim
  electronics
  intel4004
)

target_compile_definitions(
  TestSimElectronicsIntel4004Gate
  PRIVATE INTEL4004_DATA_DIR="${PROJECT_SOURCE_DIR}/src/sim/electronics/intel4004/netlist/data"
)
