# ==============================================================================
# Tests: sim_environment_gravity
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Test Sources
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/ConstantGravityModel_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Egm2008Model_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/GrailModel_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/GravityUtilities_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SlimCoeffSource_uTest.cpp"
)

set(CUDA_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Egm2008ModelCuda_uTest.cu")

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

if (CUDAToolkit_FOUND)
  apex_add_gtest(
    TARGET
    TestSimEnvironmentGravity
    SOURCES
    ${UNIT_TEST_SOURCES}
    CUDA
    ${CUDA_TEST_SOURCES}
    LINK
    sim_environment_gravity
    sim_environment_gravity_cuda
    utilities_math_legendre
    utilities_math_legendre_cuda
    fmt::fmt
    COVERAGE_FOR
    sim_environment_gravity
    LABELS
    sim
    environment
    gravity
  )
else ()
  apex_add_gtest(
    TARGET
    TestSimEnvironmentGravity
    SOURCES
    ${UNIT_TEST_SOURCES}
    LINK
    sim_environment_gravity
    utilities_math_legendre
    fmt::fmt
    COVERAGE_FOR
    sim_environment_gravity
    LABELS
    sim
    environment
    gravity
  )
endif ()
