# ==============================================================================
# Unit Tests: utilities_math_quaternion
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Test Sources
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES Quaternion_uTest.cpp)

set(CUDA_TEST_SOURCES QuaternionCuda_uTest.cu)

# ------------------------------------------------------------------------------
# Test Executable
# ------------------------------------------------------------------------------

if (CUDAToolkit_FOUND)
  apex_add_gtest(
    TARGET
    TestMathQuaternion
    SOURCES
    ${UNIT_TEST_SOURCES}
    CUDA
    ${CUDA_TEST_SOURCES}
    LINK
    utilities_math_quaternion
    utilities_math_quaternion_cuda
    LABELS
    utilities
    math
    quaternion
  )
else ()
  apex_add_gtest(
    TARGET
    TestMathQuaternion
    SOURCES
    ${UNIT_TEST_SOURCES}
    LINK
    utilities_math_quaternion
    LABELS
    utilities
    math
    quaternion
  )
endif ()
