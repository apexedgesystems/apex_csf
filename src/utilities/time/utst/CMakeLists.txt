# ==============================================================================
# Tests: utilities_time (4 tests)
# ==============================================================================

set(UNIT_TEST_SOURCES TimeBase_uTest.cpp)

apex_add_gtest(
  TARGET
  TestUtilitiesTime
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  ${LIB_NAME}
  LABELS
  utilities
)
