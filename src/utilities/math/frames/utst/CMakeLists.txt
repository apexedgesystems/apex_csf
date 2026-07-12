# ==============================================================================
# Unit Tests: utilities_math_frames
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES Transform_uTest.cpp FrameGraph_uTest.cpp Catalog_uTest.cpp)

apex_add_gtest(
  TARGET
  TestMathFrames
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  utilities_math_frames
  LABELS
  utilities
  math
  frames
)
