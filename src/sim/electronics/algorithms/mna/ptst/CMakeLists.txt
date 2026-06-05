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
  apex_add_ptest(
    TARGET CudssOneShotProbe_PTEST SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/CudssOneShotProbe_pTest.cu"
    LINK sim_electronics_algorithms_mna_cuda
  )
  target_link_libraries(CudssOneShotProbe_PTEST PRIVATE CUDA::cudart cudss)
  set_target_properties(
    CudssOneShotProbe_PTEST PROPERTIES CUDA_SEPARABLE_COMPILATION ON CUDA_RESOLVE_DEVICE_SYMBOLS ON
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
endif ()
