# ==============================================================================
# Performance Tests: sim_electronics_chips_intel4004_gate
#
# Gate extraction and event-driven NOR propagation throughput for the 427-gate
# Intel 4004 gate-level model.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Intel4004Gate_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET
  Intel4004Gate_PTEST
  SOURCES
  ${PERF_TEST_SOURCES}
  LINK
  sim_electronics_chips_intel4004_gate
  sim_electronics_chips_intel4004_netlist
)

target_compile_definitions(
  Intel4004Gate_PTEST
  PRIVATE INTEL4004_DATA_DIR="${PROJECT_SOURCE_DIR}/src/sim/electronics/chips/intel4004/netlist/data"
)
