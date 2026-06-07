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

# CPU tests: separate binary, no _cuda linkage -> runs on any host.
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

# CUDA tests: separate binary linking the _cuda sidecar (keeps the CPU tests
# above driver-free). Labeled cuda so the test runner can exclude them where
# no GPU is present.
if (CUDAToolkit_FOUND AND TARGET utilities_math_quaternion_cuda)
  apex_add_gtest(
    TARGET
    TestMathQuaternionCuda
    SOURCES
    ${CUDA_TEST_SOURCES}
    LINK
    utilities_math_quaternion
    utilities_math_quaternion_cuda
    LABELS
    utilities
    math
    quaternion
    cuda
  )
  set_target_properties(
    TestMathQuaternionCuda PROPERTIES CUDA_SEPARABLE_COMPILATION ON CUDA_RESOLVE_DEVICE_SYMBOLS ON
  )
endif ()
