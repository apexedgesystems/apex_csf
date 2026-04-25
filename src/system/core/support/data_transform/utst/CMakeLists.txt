# ==============================================================================
# Unit Tests: system_core_support_data_transform
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/DataTransform_uTest.cpp")

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreSupportDataTransform
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_support_data_transform
  fmt::fmt
  LABELS
  system
  support
  data_transform
)
