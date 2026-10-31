# ==============================================================================
# Performance Tests: utilities_math_legendre
#
# Throughput of the scalar functions (CPU) and of the normalized Pbar triangle
# (CPU and GPU).
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/LegendrePbar_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

# Scalar functions: CPU only, so it builds and runs on any host.
apex_add_ptest(
  TARGET LegendreScalar_PTEST SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/LegendreScalar_pTest.cpp" LINK
  utilities_math_legendre
)

# Triangle CPU and GPU paths.
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
