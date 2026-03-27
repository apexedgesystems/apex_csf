# ==============================================================================
# GPU Compute Simulation Models
# ==============================================================================
#
# CUDA-accelerated workload models for edge compute demonstration.
# Each model dispatches GPU kernels via SchedulableTaskCUDA and provides
# async kick/poll execution. CPU fallback stubs when CUDA unavailable.
#
# Models:
#   - BatchStatsModel:    Parallel reduction (min/max/mean/var/histogram)
#   - ConvFilterModel:    2D image convolution with shared memory tiling
#   - FFTAnalyzerModel:   Batched 1D FFT + spectral peak detection
#   - StreamCompactModel: Threshold + prefix-sum compaction + histogram
#
# ==============================================================================

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/batch_stats/CMakeLists.txt")
  add_subdirectory(batch_stats)
endif ()

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/conv_filter/CMakeLists.txt")
  add_subdirectory(conv_filter)
endif ()

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/fft_analyzer/CMakeLists.txt")
  add_subdirectory(fft_analyzer)
endif ()

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/stream_compact/CMakeLists.txt")
  add_subdirectory(stream_compact)
endif ()

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ptst/CMakeLists.txt")
  add_subdirectory(ptst)
endif ()
