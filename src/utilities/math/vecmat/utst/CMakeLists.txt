# ==============================================================================
# Unit Tests: utilities_math_vecmat
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES Vecmat_uTest.cpp)

apex_add_gtest(
  TARGET
  TestMathVecmat
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  utilities_math_vecmat
  utilities_math_quaternion
  utilities_math_linalg
  LABELS
  utilities
  math
  vecmat
)
