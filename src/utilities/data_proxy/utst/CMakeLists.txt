# ==============================================================================
# Unit Tests: utilities_data_proxy
#
# Tests data proxy infrastructure (ByteMaskProxy, EndiannessProxy,
# MasterDataProxy).
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources (3 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/ByteMaskProxy_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/EndiannessProxy_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MasterDataProxy_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreDataProxy
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  utilities_data_proxy
  fmt::fmt
  LABELS
  system
  data_proxy
)
