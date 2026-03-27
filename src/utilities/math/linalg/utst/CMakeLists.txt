# ==============================================================================
# Unit Tests: utilities_math_linalg
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Test Sources
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    ArrayBase_uTest.cpp
    Array_uTest.cpp
    Vector_uTest.cpp
    Matrix2_uTest.cpp
    Matrix3_uTest.cpp
    Matrix4_uTest.cpp
    Rotations_uTest.cpp
)

set(CUDA_TEST_SOURCES ArrayCuda_uTest.cu)

# ------------------------------------------------------------------------------
# Test Executable
# ------------------------------------------------------------------------------

if (CUDAToolkit_FOUND)
  apex_add_gtest(
    TARGET
    TestLinalgArray
    SOURCES
    ${UNIT_TEST_SOURCES}
    CUDA
    ${CUDA_TEST_SOURCES}
    LINK
    utilities_math_linalg
    utilities_math_linalg_cuda
    LABELS
    utilities
    math
    linalg
  )
else ()
  apex_add_gtest(
    TARGET
    TestLinalgArray
    SOURCES
    ${UNIT_TEST_SOURCES}
    LINK
    utilities_math_linalg
    LABELS
    utilities
    math
    linalg
  )
endif ()
