# ==============================================================================
# Development Tests: sim_electronics_intel4004_grid
#
# Full-circuit comparison tests (behavioral vs transistor-level). Each test
# simulates 2,242 transistors for 50-70 seconds. Manual execution only.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Test Sources
# ------------------------------------------------------------------------------

set(DEV_TEST_SOURCES Intel4004Grid_uTest.cpp Intel4004GridVsBehavioral_uTest.cpp
                     Intel4004GridLevel2_uTest.cpp Intel4004OprDecode_dTest.cpp)

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_devtest(
  TARGET
  Intel4004Grid_Dev
  SOURCES
  ${DEV_TEST_SOURCES}
  LINK
  sim_electronics_intel4004_grid
  sim_electronics_intel4004_netlist
  sim_electronics_algorithms_spice_ngspice
  fmt::fmt
)

target_compile_definitions(
  Intel4004Grid_Dev
  PRIVATE INTEL4004_DATA_DIR="${PROJECT_SOURCE_DIR}/src/sim/electronics/intel4004/netlist/data"
)
