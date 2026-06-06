# ==============================================================================
# Performance Tests: sim_electronics_algorithms_mna
#
# Dense (LAPACK) and sparse (KLU) solver benchmarks at varying circuit sizes.
# Measures factorize, solve, cached back-sub, and stamping throughput.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Mna_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(TARGET Mna_PTEST SOURCES ${PERF_TEST_SOURCES} LINK sim_electronics_algorithms_mna)

# ------------------------------------------------------------------------------
# CUDA Performance Tests (requires CUDA toolkit + sim_electronics_algorithms_mna_cuda)
# ------------------------------------------------------------------------------

if (TARGET sim_electronics_algorithms_mna_cuda AND CMAKE_CUDA_COMPILER)
  set(CUDA_PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/MnaCuda_pTest.cu")

  apex_add_ptest(
    TARGET
    MnaCuda_PTEST
    SOURCES
    ${CUDA_PERF_TEST_SOURCES}
    LINK
    sim_electronics_algorithms_mna_cuda
    sim_electronics_algorithms_mna
    bench_cuda
  )

  set_target_properties(
    MnaCuda_PTEST PROPERTIES CUDA_SEPARABLE_COMPILATION ON CUDA_RESOLVE_DEVICE_SYMBOLS ON
  )

  # cuDSS sparse-LU probes (successors to the deprecated cuSolverSp/Rf probes).
  # cuDSS does reordering + symbolic factorization internally (ANALYSIS), then
  # re-uses it for new values (REFACTORIZATION) -- the Phase 4 Newton pattern.
  #
  # cuDSS ships x86_64-only (installed from the NVIDIA redist tarball in the
  # cuda dev image). The Jetson dev image is FROM the cuda image, so it inherits
  # that x86_64 libcudss -- find_library would locate it even while we cross-
  # compile for aarch64, and linking a host x86_64 lib into an aarch64 target
  # fails. So gate on both: the library being findable AND a non-cross (native
  # x86_64) build. cudss lives under the CUDA toolkit lib dir
  # (e.g. /usr/local/cuda/lib64), which find_library does not search by default
  # -- hint it explicitly.
  find_library(
    CUDSS_LIBRARY
    NAMES cudss
    HINTS "${CUDAToolkit_LIBRARY_DIR}" /usr/local/cuda/lib64
  )
  if (CUDSS_LIBRARY AND NOT CMAKE_CROSSCOMPILING)
    apex_add_ptest(
      TARGET CudssOneShotProbe_PTEST SOURCES
      "${CMAKE_CURRENT_SOURCE_DIR}/CudssOneShotProbe_pTest.cu" LINK
      sim_electronics_algorithms_mna_cuda
    )
    target_link_libraries(CudssOneShotProbe_PTEST PRIVATE CUDA::cudart cudss)
    set_target_properties(
      CudssOneShotProbe_PTEST PROPERTIES CUDA_SEPARABLE_COMPILATION ON CUDA_RESOLVE_DEVICE_SYMBOLS
                                                                       ON
    )

    apex_add_ptest(
      TARGET CudssRefactorProbe_PTEST SOURCES
      "${CMAKE_CURRENT_SOURCE_DIR}/CudssRefactorProbe_pTest.cu" LINK
      sim_electronics_algorithms_mna_cuda
    )
    target_link_libraries(CudssRefactorProbe_PTEST PRIVATE CUDA::cudart cudss)
    set_target_properties(
      CudssRefactorProbe_PTEST PROPERTIES CUDA_SEPARABLE_COMPILATION ON CUDA_RESOLVE_DEVICE_SYMBOLS
                                                                        ON
    )

    apex_add_ptest(
      TARGET CudssBatchProbe_PTEST SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/CudssBatchProbe_pTest.cu"
      LINK sim_electronics_algorithms_mna_cuda
    )
    target_link_libraries(CudssBatchProbe_PTEST PRIVATE CUDA::cudart cudss)
    set_target_properties(
      CudssBatchProbe_PTEST PROPERTIES CUDA_SEPARABLE_COMPILATION ON CUDA_RESOLVE_DEVICE_SYMBOLS ON
    )
  else ()
    message(
      STATUS
        "electronics/mna: skipping cuDSS sparse-LU probes (need native x86_64 build with libcudss)"
    )
  endif ()
endif ()
