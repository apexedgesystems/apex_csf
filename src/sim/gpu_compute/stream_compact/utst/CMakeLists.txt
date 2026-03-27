# ==============================================================================
# Unit Tests: sim_gpu_compute_stream_compact
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (2 files)
# ------------------------------------------------------------------------------

set(CUDA_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/StreamCompactKernel_uTest.cu")

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/StreamCompactModel_uTest.cpp")

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

if (CUDAToolkit_FOUND AND TARGET sim_gpu_compute_stream_compact_cuda)
  apex_add_gtest(
    TARGET
    TestSimGpuComputeStreamCompactKernel
    SOURCES
    ${CUDA_TEST_SOURCES}
    LINK
    sim_gpu_compute_stream_compact_cuda
    fmt::fmt
    LABELS
    sim
    gpu_compute
    stream_compact
  )

  apex_add_gtest(
    TARGET
    TestSimGpuComputeStreamCompactModel
    SOURCES
    ${UNIT_TEST_SOURCES}
    LINK
    sim_gpu_compute_stream_compact
    sim_gpu_compute_stream_compact_cuda
    fmt::fmt
    LABELS
    sim
    gpu_compute
    stream_compact
  )
else ()
  apex_add_gtest(
    TARGET
    TestSimGpuComputeStreamCompactModel
    SOURCES
    ${UNIT_TEST_SOURCES}
    LINK
    sim_gpu_compute_stream_compact
    fmt::fmt
    LABELS
    sim
    gpu_compute
    stream_compact
  )
endif ()
