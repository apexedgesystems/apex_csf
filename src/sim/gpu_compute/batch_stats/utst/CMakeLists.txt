# ==============================================================================
# Unit Tests: sim_gpu_compute_batch_stats
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (2 files)
# ------------------------------------------------------------------------------

set(CUDA_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/BatchStatsKernel_uTest.cu")

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/BatchStatsModel_uTest.cpp")

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

if (CUDAToolkit_FOUND AND TARGET sim_gpu_compute_batch_stats_cuda)
  apex_add_gtest(
    TARGET
    TestSimGpuComputeBatchStatsKernel
    SOURCES
    ${CUDA_TEST_SOURCES}
    LINK
    sim_gpu_compute_batch_stats_cuda
    fmt::fmt
    LABELS
    sim
    gpu_compute
    batch_stats
  )

  apex_add_gtest(
    TARGET
    TestSimGpuComputeBatchStatsModel
    SOURCES
    ${UNIT_TEST_SOURCES}
    LINK
    sim_gpu_compute_batch_stats
    sim_gpu_compute_batch_stats_cuda
    fmt::fmt
    LABELS
    sim
    gpu_compute
    batch_stats
  )
else ()
  apex_add_gtest(
    TARGET
    TestSimGpuComputeBatchStatsModel
    SOURCES
    ${UNIT_TEST_SOURCES}
    LINK
    sim_gpu_compute_batch_stats
    fmt::fmt
    LABELS
    sim
    gpu_compute
    batch_stats
  )
endif ()
