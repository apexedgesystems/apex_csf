# ==============================================================================
# Dev Tests: sim_electronics_chips_intel4004_gate
#
# Component-level tests: instruction execution, cross-level comparison.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(DEV_TEST_SOURCES GateLevelExecution_dTest.cpp)

# ------------------------------------------------------------------------------
# Dev Tests
# ------------------------------------------------------------------------------

apex_add_devtest(
  TARGET
  Intel4004GateLevel_Dev
  SOURCES
  ${DEV_TEST_SOURCES}
  LINK
  sim_electronics_chips_intel4004_gate
  sim_electronics_chips_intel4004_behavioral
  sim_electronics_chips_intel4004_netlist
  fmt::fmt
)

target_compile_definitions(
  Intel4004GateLevel_Dev
  PRIVATE INTEL4004_DATA_DIR="${PROJECT_SOURCE_DIR}/src/sim/electronics/chips/intel4004/netlist/data"
)
