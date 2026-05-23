# ==============================================================================
# Performance Tests: sim_electronics_chips_intel4004_grid
#
# Circuit build, single-byte L1 execution, and full stamp throughput for the
# 2,242-transistor Intel 4004 grid.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Intel4004Grid_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET
  Intel4004Grid_PTEST
  SOURCES
  ${PERF_TEST_SOURCES}
  LINK
  sim_electronics_chips_intel4004_grid
  sim_electronics_chips_intel4004_netlist
)

target_compile_definitions(
  Intel4004Grid_PTEST
  PRIVATE
    INTEL4004_DATA_DIR="${PROJECT_SOURCE_DIR}/src/sim/electronics/chips/intel4004/netlist/data"
)

# ------------------------------------------------------------------------------
# Phase 4 GPU-resident NR inner-iter perf (CUDA; gitignored like other ptests)
# ------------------------------------------------------------------------------

if (TARGET sim_electronics_chips_intel4004_grid_cuda AND CMAKE_CUDA_COMPILER)
  apex_add_ptest(
    TARGET
    PhaseFourInnerIter_PTEST
    SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/PhaseFourInnerIter_pTest.cu"
    LINK
    sim_electronics_chips_intel4004_grid_cuda
    sim_electronics_devices_nonlinear_cuda
    sim_electronics_algorithms_mna_cuda
    sim_electronics_devices_nonlinear
    sim_electronics_algorithms_mna
    bench_cuda
  )
  set_target_properties(
    PhaseFourInnerIter_PTEST PROPERTIES CUDA_SEPARABLE_COMPILATION ON CUDA_RESOLVE_DEVICE_SYMBOLS
                                                                      ON
  )
endif ()
