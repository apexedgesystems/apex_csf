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

if (CUDAToolkit_FOUND)
  apex_add_gtest(
    TARGET
    TestMathLegendre
    SOURCES
    ${UNIT_TEST_SOURCES}
    CUDA
    ${CUDA_TEST_SOURCES}
    LINK
    utilities_math_legendre
    utilities_math_legendre_cuda
    fmt::fmt
    COVERAGE_FOR
    utilities_math_legendre
    LABELS
    utilities
    math
    legendre
  )
else ()
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
endif ()
