# ==============================================================================
# Unit Tests: sim_electronics_intel4004_grid
#
# L1 stamp validation against ngspice (Level1Physics) plus Shichman-Hodges
# parameter sanity, NR sub-region behavior, and component classification.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (4 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Level1Stamp_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Level1Params_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Level1Physics_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ComponentClassification_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSimElectronicsIntel4004Grid
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_electronics_intel4004_grid
  sim_electronics_intel4004_netlist
  LABELS
  sim
  electronics
  intel4004
)

target_compile_definitions(
  TestSimElectronicsIntel4004Grid
  PRIVATE INTEL4004_DATA_DIR="${PROJECT_SOURCE_DIR}/src/sim/electronics/intel4004/netlist/data"
)
