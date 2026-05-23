# ==============================================================================
# Unit Tests: sim_electronics_chips_intel4004_grid
#
# L1 stamp validation against ngspice (Level1Physics) plus Shichman-Hodges
# parameter sanity, NR sub-region behavior, and component classification.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (8 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Level1Stamp_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Level1Params_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Level1Physics_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Level2Physics_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ComponentClassification_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/BootstrapCapsCompliance_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LogicLevelCompliance_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PmosDynamicLatch_uTest.cpp"
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
  sim_electronics_chips_intel4004_grid
  sim_electronics_chips_intel4004_netlist
  LABELS
  sim
  electronics
  intel4004
)

target_compile_definitions(
  TestSimElectronicsIntel4004Grid
  PRIVATE
    INTEL4004_DATA_DIR="${PROJECT_SOURCE_DIR}/src/sim/electronics/chips/intel4004/netlist/data"
)

# ------------------------------------------------------------------------------
# CUDA Integration Tests (Phase 4 pipeline)
# ------------------------------------------------------------------------------

if (TARGET sim_electronics_chips_intel4004_grid_cuda AND CMAKE_CUDA_COMPILER)
  apex_add_gtest(
    TARGET
    sim_electronics_chips_intel4004_grid_cuda_uTest
    SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/PhaseFourPipeline_uTest.cu"
    LINK
    sim_electronics_chips_intel4004_grid_cuda
    sim_electronics_chips_intel4004_netlist
    sim_electronics_devices_nonlinear_cuda
    sim_electronics_algorithms_mna_cuda
    sim_electronics_devices_nonlinear
    LABELS
    sim
    electronics
    intel4004
    cuda
  )
  set_target_properties(
    sim_electronics_chips_intel4004_grid_cuda_uTest PROPERTIES CUDA_SEPARABLE_COMPILATION ON
                                                               CUDA_RESOLVE_DEVICE_SYMBOLS ON
  )
  target_compile_definitions(
    sim_electronics_chips_intel4004_grid_cuda_uTest
    PRIVATE
      INTEL4004_DATA_DIR="${PROJECT_SOURCE_DIR}/src/sim/electronics/chips/intel4004/netlist/data"
  )
endif ()
