# ==============================================================================
# Unit Tests: sim_gpu_compute_conv_filter
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (2 files)
# ------------------------------------------------------------------------------

set(CUDA_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ConvFilterKernel_uTest.cu")

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ConvFilterModel_uTest.cpp")

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

if (CUDAToolkit_FOUND AND TARGET sim_gpu_compute_conv_filter_cuda)
  apex_add_gtest(
    TARGET
    TestSimGpuComputeConvFilterKernel
    SOURCES
    ${CUDA_TEST_SOURCES}
    LINK
    sim_gpu_compute_conv_filter_cuda
    fmt::fmt
    LABELS
    sim
    gpu_compute
    conv_filter
  )

  apex_add_gtest(
    TARGET
    TestSimGpuComputeConvFilterModel
    SOURCES
    ${UNIT_TEST_SOURCES}
    LINK
    sim_gpu_compute_conv_filter
    sim_gpu_compute_conv_filter_cuda
    fmt::fmt
    LABELS
    sim
    gpu_compute
    conv_filter
  )
else ()
  apex_add_gtest(
    TARGET
    TestSimGpuComputeConvFilterModel
    SOURCES
    ${UNIT_TEST_SOURCES}
    LINK
    sim_gpu_compute_conv_filter
    fmt::fmt
    LABELS
    sim
    gpu_compute
    conv_filter
  )
endif ()
