# ==============================================================================
# Unit Tests: utilities_build_info
#
# The identity strings of the build these tests were built in.
# ==============================================================================

include(apex/All)

apex_add_gtest(
  TARGET
  TestUtilitiesBuildInfo
  SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/BuildInfo_uTest.cpp"
  LINK
  utilities_build_info
  LABELS
  utilities
  build_info
)
