# ==============================================================================
# Unit Tests: sim_gpu_compute_fft_analyzer
# ==============================================================================

include(apex/All)

# cuFFT resolution: CUDA::cufft resolves to x86 stubs when cross-compiling.
# Use sbsa/aarch64 stubs instead; Thor runtime provides cuFFT shared lib.
if (CMAKE_CROSSCOMPILING)
  find_library(
    _CUFFT_TEST_LIB
    NAMES cufft
    PATHS /usr/local/cuda/targets/sbsa-linux/lib/stubs
          /usr/local/cuda/targets/aarch64-linux/lib/stubs
    NO_DEFAULT_PATH
  )
  set(_CUFFT_LINK "${_CUFFT_TEST_LIB}")
elseif (TARGET CUDA::cufft)
  set(_CUFFT_LINK CUDA::cufft)
else ()
  set(_CUFFT_LINK "")
endif ()

# ------------------------------------------------------------------------------
# Sources (2 files)
# ------------------------------------------------------------------------------

set(CUDA_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/FFTAnalyzerKernel_uTest.cu")

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/FFTAnalyzerModel_uTest.cpp")

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

if (CUDAToolkit_FOUND
    AND TARGET sim_gpu_compute_fft_analyzer_cuda
    AND _CUFFT_LINK
)
  apex_add_gtest(
    TARGET
    TestSimGpuComputeFFTAnalyzerKernel
    SOURCES
    ${CUDA_TEST_SOURCES}
    LINK
    sim_gpu_compute_fft_analyzer_cuda
    ${_CUFFT_LINK}
    fmt::fmt
    LABELS
    sim
    gpu_compute
    fft_analyzer
  )

  apex_add_gtest(
    TARGET
    TestSimGpuComputeFFTAnalyzerModel
    SOURCES
    ${UNIT_TEST_SOURCES}
    LINK
    sim_gpu_compute_fft_analyzer
    sim_gpu_compute_fft_analyzer_cuda
    ${_CUFFT_LINK}
    fmt::fmt
    LABELS
    sim
    gpu_compute
    fft_analyzer
  )
else ()
  apex_add_gtest(
    TARGET
    TestSimGpuComputeFFTAnalyzerModel
    SOURCES
    ${UNIT_TEST_SOURCES}
    LINK
    sim_gpu_compute_fft_analyzer
    fmt::fmt
    LABELS
    sim
    gpu_compute
    fft_analyzer
  )
endif ()
