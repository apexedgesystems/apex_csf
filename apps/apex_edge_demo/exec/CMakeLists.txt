# ==============================================================================
# App: ApexEdgeDemo
#
# POSIX executive for the edge compute GPU demonstration. Runs GPU workload
# models under ApexExecutive with TPRM-driven configuration.
#
# Build:
#   make compose-debug
#
# Run:
#   ./build/native-linux-debug/bin/ApexEdgeDemo \
#     --config apps/apex_edge_demo/tprm/master.tprm --shutdown-after 10
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (2 files)
# ------------------------------------------------------------------------------

set(APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/EdgeExecutive.cpp"
)

# ------------------------------------------------------------------------------
# Executable
# ------------------------------------------------------------------------------

apex_add_app(
  NAME
  ApexEdgeDemo
  SRC
  ${APP_SOURCES}
  INC
  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  LINK
  sim_gpu_compute_batch_stats
  sim_gpu_compute_conv_filter
  sim_gpu_compute_fft_analyzer
  sim_gpu_compute_stream_compact
  system_core_executive
  system_core_scheduler
  system_core_support_system_monitor
  system_core_logs
  system_core_filesystem
  utilities_helpers
  utilities_concurrency
  utilities_compatibility
  fmt::fmt
  REQUIRES
  POSIX
)

# Link CUDA libraries if available
if (TARGET sim_gpu_compute_batch_stats_cuda)
  target_link_libraries(ApexEdgeDemo PRIVATE sim_gpu_compute_batch_stats_cuda)
endif ()

if (TARGET sim_gpu_compute_conv_filter_cuda)
  target_link_libraries(ApexEdgeDemo PRIVATE sim_gpu_compute_conv_filter_cuda)
endif ()

if (TARGET sim_gpu_compute_fft_analyzer_cuda)
  target_link_libraries(ApexEdgeDemo PRIVATE sim_gpu_compute_fft_analyzer_cuda)
  if (CMAKE_CROSSCOMPILING)
    find_library(
      _CUFFT_CROSS_APP
      NAMES cufft
      PATHS /usr/local/cuda/targets/sbsa-linux/lib/stubs
            /usr/local/cuda/targets/aarch64-linux/lib/stubs
      NO_DEFAULT_PATH
    )
    if (_CUFFT_CROSS_APP)
      target_link_libraries(ApexEdgeDemo PRIVATE ${_CUFFT_CROSS_APP})
    endif ()
  elseif (TARGET CUDA::cufft)
    target_link_libraries(ApexEdgeDemo PRIVATE CUDA::cufft)
  endif ()
endif ()

if (TARGET sim_gpu_compute_stream_compact_cuda)
  target_link_libraries(ApexEdgeDemo PRIVATE sim_gpu_compute_stream_compact_cuda)
endif ()
