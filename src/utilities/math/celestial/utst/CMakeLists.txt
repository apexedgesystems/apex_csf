# ==============================================================================
# Unit Tests: utilities_math_celestial
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES Celestial_uTest.cpp)

apex_add_gtest(
  TARGET
  TestMathCelestial
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  utilities_math_celestial
  utilities_math_vecmat
  LABELS
  utilities
  math
  celestial
)
