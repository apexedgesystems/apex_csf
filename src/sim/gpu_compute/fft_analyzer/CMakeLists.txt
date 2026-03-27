# ==============================================================================
# Module: sim_gpu_compute_fft_analyzer
#
# GPU batched FFT analyzer: cuFFT R2C transform + custom magnitude/peak kernel.
# Signal processing workload for vibration monitoring and spectral analysis.
#
# Libraries:
#   sim_gpu_compute_fft_analyzer      - Header-only model interface
#   sim_gpu_compute_fft_analyzer_cuda - CUDA kernel + cuFFT dependency
# ==============================================================================

include(apex/All)
apex_module(sim_gpu_compute_fft_analyzer)

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
# cuFFT cross-compile: CUDA::cufft resolves to x86 stubs on aarch64.
# Use sbsa/aarch64 stubs instead. Thor runtime provides cuFFT shared lib.
# ------------------------------------------------------------------------------

if (CUDAToolkit_FOUND)
  apex_add_library_cuda(
    NAME
    ${LIB_NAME}_cuda
    CORE
    ${LIB_NAME}
    SRC
    "${SRC_DIR}/FFTAnalyzerKernel.cu"
    NO_NVML
  )

  if (CMAKE_CROSSCOMPILING)
    find_library(
      _CUFFT_CROSS_LIB
      NAMES cufft
      PATHS /usr/local/cuda/targets/sbsa-linux/lib/stubs
            /usr/local/cuda/targets/aarch64-linux/lib/stubs
      NO_DEFAULT_PATH
    )
    if (_CUFFT_CROSS_LIB)
      target_link_libraries(${LIB_NAME}_cuda PRIVATE ${_CUFFT_CROSS_LIB})
    endif ()
  elseif (TARGET CUDA::cufft)
    target_link_libraries(${LIB_NAME}_cuda PRIVATE CUDA::cufft)
  endif ()
endif ()

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst)
