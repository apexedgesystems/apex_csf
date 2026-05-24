# ==============================================================================
# Performance Tests: utilities_math_integration
#
# Throughput benchmarks for ODE integrators (Euler, RK4, RK45, Leapfrog, RKN4).
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Integration_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET Integration_PTEST SOURCES ${PERF_TEST_SOURCES} LINK utilities_math_integration
)
