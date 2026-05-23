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

  # cuSolverSp probe (Phase 4 sparse-LU exploration; links cusolver + cusparse directly)
  apex_add_ptest(
    TARGET CuSolverSpProbe_PTEST SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/CuSolverSpProbe_pTest.cu"
    LINK sim_electronics_algorithms_mna_cuda
  )
  target_link_libraries(CuSolverSpProbe_PTEST PRIVATE CUDA::cudart CUDA::cusolver CUDA::cusparse)
  set_target_properties(
    CuSolverSpProbe_PTEST PROPERTIES CUDA_SEPARABLE_COMPILATION ON CUDA_RESOLVE_DEVICE_SYMBOLS ON
  )

  # cusolverRf probe (analyse-once + refactor-per-iter; the right API for Phase 4 NR loops)
  apex_add_ptest(
    TARGET CuSolverRfProbe_PTEST SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/CuSolverRfProbe_pTest.cu"
    LINK sim_electronics_algorithms_mna_cuda
  )
  target_link_libraries(CuSolverRfProbe_PTEST PRIVATE CUDA::cudart CUDA::cusolver CUDA::cusparse)
  set_target_properties(
    CuSolverRfProbe_PTEST PROPERTIES CUDA_SEPARABLE_COMPILATION ON CUDA_RESOLVE_DEVICE_SYMBOLS ON
  )

  # cusolverRfBatch probe (K parallel circuits; Monte Carlo / parameter sweep target)
  apex_add_ptest(
    TARGET CuSolverRfBatchProbe_PTEST SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/CuSolverRfBatchProbe_pTest.cu" LINK
    sim_electronics_algorithms_mna_cuda
  )
  target_link_libraries(
    CuSolverRfBatchProbe_PTEST PRIVATE CUDA::cudart CUDA::cusolver CUDA::cusparse
  )
  set_target_properties(
    CuSolverRfBatchProbe_PTEST PROPERTIES CUDA_SEPARABLE_COMPILATION ON CUDA_RESOLVE_DEVICE_SYMBOLS
                                                                        ON
  )
endif ()
