# ==============================================================================
# Unit Tests: utilities_math_linalg
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Test Sources
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES ArrayBase_uTest.cpp Array_uTest.cpp Vector_uTest.cpp Matrix2_uTest.cpp
                      Matrix3_uTest.cpp Matrix4_uTest.cpp
)

set(CUDA_TEST_SOURCES ArrayCuda_uTest.cu)

# ------------------------------------------------------------------------------
# Test Executable
# ------------------------------------------------------------------------------

# CPU tests: separate binary, no _cuda linkage -> runs on any host.
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

# CUDA tests: separate binary linking the _cuda sidecar (keeps the CPU tests
# above driver-free). Labeled cuda so the test runner can exclude them where
# no GPU is present.
if (CUDAToolkit_FOUND AND TARGET utilities_math_linalg_cuda)
  apex_add_gtest(
    TARGET
    TestLinalgArrayCuda
    SOURCES
    ${CUDA_TEST_SOURCES}
    LINK
    utilities_math_linalg
    utilities_math_linalg_cuda
    LABELS
    utilities
    math
    linalg
    cuda
  )
  set_target_properties(
    TestLinalgArrayCuda PROPERTIES CUDA_SEPARABLE_COMPILATION ON CUDA_RESOLVE_DEVICE_SYMBOLS ON
  )
endif ()
