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

# CPU tests: separate binary, no _cuda linkage -> runs on any host.
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

# CUDA tests: separate binary linking the _cuda sidecar (keeps the CPU tests
# above driver-free). Labeled cuda so the test runner can exclude them where
# no GPU is present.
if (CUDAToolkit_FOUND AND TARGET sim_environment_gravity_cuda)
  apex_add_gtest(
    TARGET
    TestSimEnvironmentGravityCuda
    SOURCES
    ${CUDA_TEST_SOURCES}
    LINK
    sim_environment_gravity
    sim_environment_gravity_cuda
    utilities_math_legendre
    utilities_math_legendre_cuda
    fmt::fmt
    LABELS
    sim
    environment
    gravity
    cuda
  )
  set_target_properties(
    TestSimEnvironmentGravityCuda PROPERTIES CUDA_SEPARABLE_COMPILATION ON
                                             CUDA_RESOLVE_DEVICE_SYMBOLS ON
  )
endif ()
