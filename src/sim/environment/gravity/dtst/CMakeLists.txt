# ==============================================================================
# Module: sim_environment_gravity (dtst)
#
# Development tests for integration and validation. These tests require external
# data files (EGM2008 coefficients) and are NOT registered with CTest.
#
# Run manually:
#   ./build/native-linux-debug/bin/devtests/SimEnvironmentGravity_Dev
#
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(DEV_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/00_Egm2008MaxFidelity_dTest.cpp")

set(CUDA_DEV_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/00_Egm2008CudaTiming_dTest.cu")

# ------------------------------------------------------------------------------
# Development Tests (manual execution only)
# ------------------------------------------------------------------------------

if (CUDAToolkit_FOUND)
  apex_add_devtest(
    TARGET
    SimEnvironmentGravity_Dev
    SOURCES
    ${DEV_TEST_SOURCES}
    CUDA
    ${CUDA_DEV_TEST_SOURCES}
    LINK
    sim_environment_gravity
    sim_environment_gravity_cuda
    utilities_math_legendre
    utilities_math_legendre_cuda
    fmt::fmt
  )
else ()
  apex_add_devtest(
    TARGET
    SimEnvironmentGravity_Dev
    SOURCES
    ${DEV_TEST_SOURCES}
    LINK
    sim_environment_gravity
    utilities_math_legendre
    fmt::fmt
  )
endif ()
