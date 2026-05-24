# ==============================================================================
# Performance Tests: sim_gpu_compute
#
# Consolidated GPU kernel benchmarks: batch stats reduction, 2D convolution,
# batched cuFFT analyzer, and stream-compaction throughput.
# ==============================================================================

include(apex/All)

# cuFFT resolution for cross-compile
if (CMAKE_CROSSCOMPILING)
  find_library(
    _CUFFT_PTEST_LIB
    NAMES cufft
    PATHS /usr/local/cuda/targets/sbsa-linux/lib/stubs
          /usr/local/cuda/targets/aarch64-linux/lib/stubs
    NO_DEFAULT_PATH
  )
  set(_CUFFT_LINK "${_CUFFT_PTEST_LIB}")
elseif (TARGET CUDA::cufft)
  set(_CUFFT_LINK CUDA::cufft)
else ()
  set(_CUFFT_LINK "")
endif ()

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/GpuCompute_pTest.cu")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

if (CUDAToolkit_FOUND AND _CUFFT_LINK)
  apex_add_ptest(
    TARGET
    GpuCompute_PTEST
    SOURCES
    ${PERF_TEST_SOURCES}
    LINK
    sim_gpu_compute_batch_stats_cuda
    sim_gpu_compute_conv_filter_cuda
    sim_gpu_compute_fft_analyzer_cuda
    sim_gpu_compute_stream_compact_cuda
    bench_cuda
    ${_CUFFT_LINK}
    fmt::fmt
  )
endif ()
