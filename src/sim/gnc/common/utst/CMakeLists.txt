# ==============================================================================
# Unit Tests: sim_gnc_common
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/PIDLoop_uTest.cpp")

apex_add_gtest(
  TARGET
  TestSimGncCommon
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_gnc_common
  LABELS
  sim
  gnc
)
