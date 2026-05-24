# ==============================================================================
# Performance Tests: utilities_math_linalg
#
# CPU and GPU throughput benchmarks for batched linear algebra operations.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Linalg_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

if (CUDAToolkit_FOUND)
  apex_add_ptest(
    TARGET
    Linalg_PTEST
    SOURCES
    ${PERF_TEST_SOURCES}
    LINK
    utilities_math_linalg
    utilities_math_linalg_cuda
    vernier::bench_cuda
  )
endif ()
