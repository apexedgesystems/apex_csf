# ==============================================================================
# Performance Tests: utilities_math_legendre
#
# CPU and GPU throughput benchmarks for normalized Legendre Pbar triangles.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/LegendrePbar_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

if (CUDAToolkit_FOUND)
  apex_add_ptest(
    TARGET
    LegendrePbar_PTEST
    SOURCES
    ${PERF_TEST_SOURCES}
    LINK
    utilities_math_legendre
    utilities_math_legendre_cuda
    vernier::bench_cuda
  )
endif ()
