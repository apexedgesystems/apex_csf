# ==============================================================================
# Performance Tests: sim_environment_gravity
#
# Spherical harmonic gravity model throughput (EGM2008 Earth, GRAIL Moon) and
# utility model (J2, zonal, geoid) benchmarks, with optional CUDA acceleration.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (4 files)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Egm2008Model_pTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/GrailModel_pTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/GravityUtilities_pTest.cpp"
)

set(CUDA_PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Egm2008ModelCuda_pTest.cu")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

if (CUDAToolkit_FOUND)
  apex_add_ptest(
    TARGET
    SimEnvironmentGravity_PTEST
    SOURCES
    ${PERF_TEST_SOURCES}
    ${CUDA_PERF_TEST_SOURCES}
    LINK
    sim_environment_gravity
    sim_environment_gravity_cuda
    utilities_math_legendre
    utilities_math_legendre_cuda
    bench_cuda
    fmt::fmt
  )
else ()
  apex_add_ptest(
    TARGET
    SimEnvironmentGravity_PTEST
    SOURCES
    ${PERF_TEST_SOURCES}
    LINK
    sim_environment_gravity
    utilities_math_legendre
    fmt::fmt
  )
endif ()
