# ==============================================================================
# Performance Tests: system_core_support_data_transform
#
# Matched cost of the hold path against the block owner's write.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/DataTransformHold_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET DataTransformHold_PTEST SOURCES ${PERF_TEST_SOURCES} LINK
  system_core_support_data_transform
)
