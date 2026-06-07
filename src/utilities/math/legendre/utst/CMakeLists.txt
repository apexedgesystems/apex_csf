# ==============================================================================
# Tests: utilities_math_legendre
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Test Sources
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/LegendreFactorials_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LegendrePolynomials_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LegendrePbarTriangle_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LegendrePbarDerivatives_uTest.cpp"
)

set(CUDA_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/LegendrePbarCuda_uTest.cu")

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

# CPU tests: separate binary, no _cuda linkage -> runs on any host.
apex_add_gtest(
  TARGET
  TestMathLegendre
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  utilities_math_legendre
  fmt::fmt
  COVERAGE_FOR
  utilities_math_legendre
  LABELS
  utilities
  math
  legendre
)

# CUDA tests: separate binary linking the _cuda sidecar (keeps the CPU tests
# above driver-free). Labeled cuda so the test runner can exclude them where
# no GPU is present.
if (CUDAToolkit_FOUND AND TARGET utilities_math_legendre_cuda)
  apex_add_gtest(
    TARGET
    TestMathLegendreCuda
    SOURCES
    ${CUDA_TEST_SOURCES}
    LINK
    utilities_math_legendre
    utilities_math_legendre_cuda
    fmt::fmt
    LABELS
    utilities
    math
    legendre
    cuda
  )
  set_target_properties(
    TestMathLegendreCuda PROPERTIES CUDA_SEPARABLE_COMPILATION ON CUDA_RESOLVE_DEVICE_SYMBOLS ON
  )
endif ()
