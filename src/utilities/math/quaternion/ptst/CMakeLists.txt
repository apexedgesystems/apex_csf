# ==============================================================================
# Performance Tests: utilities_math_quaternion
#
# CPU and GPU throughput benchmarks for batched quaternion operations.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Quaternion_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

if (CUDAToolkit_FOUND)
  apex_add_ptest(
    TARGET
    Quaternion_PTEST
    SOURCES
    ${PERF_TEST_SOURCES}
    LINK
    utilities_math_quaternion
    utilities_math_quaternion_cuda
    vernier::bench_cuda
  )
endif ()
