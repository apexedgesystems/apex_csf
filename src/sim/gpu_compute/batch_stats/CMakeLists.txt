# ==============================================================================
# Module: sim_gpu_compute_batch_stats
#
# GPU parallel reduction model: min/max/mean/variance/histogram over large
# float arrays using warp-shuffle reduction and atomic histogram.
#
# Libraries:
#   sim_gpu_compute_batch_stats      - Header-only model interface
#   sim_gpu_compute_batch_stats_cuda - CUDA kernel implementation
# ==============================================================================

include(apex/All)
apex_module(sim_gpu_compute_batch_stats)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

target_link_libraries(
  ${LIB_NAME}
  INTERFACE system_core_system_component utilities_compatibility
)

if (CUDAToolkit_FOUND)
  target_compile_definitions(${LIB_NAME} INTERFACE COMPAT_HAVE_CUDA=1)
  if (CUDAToolkit_INCLUDE_DIRS)
    target_include_directories(${LIB_NAME} SYSTEM INTERFACE ${CUDAToolkit_INCLUDE_DIRS})
  endif ()
endif ()

# ------------------------------------------------------------------------------
# CUDA Library (requires CUDA toolkit)
# ------------------------------------------------------------------------------

if (CUDAToolkit_FOUND)
  apex_add_library_cuda(
    NAME
    ${LIB_NAME}_cuda
    CORE
    ${LIB_NAME}
    SRC
    "${SRC_DIR}/BatchStatsKernel.cu"
    NO_NVML
  )
endif ()

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst)
