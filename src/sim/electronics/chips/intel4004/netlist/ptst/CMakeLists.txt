# ==============================================================================
# Performance Tests: sim_electronics_chips_intel4004_netlist
#
# SPICE netlist parse throughput for the full 2,242-transistor Intel 4004.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Intel4004Netlist_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET Intel4004Netlist_PTEST SOURCES ${PERF_TEST_SOURCES} LINK sim_electronics_chips_intel4004_netlist
)

target_compile_definitions(
  Intel4004Netlist_PTEST
  PRIVATE INTEL4004_DATA_DIR="${PROJECT_SOURCE_DIR}/src/sim/electronics/chips/intel4004/netlist/data"
)
