# ==============================================================================
# Unit Tests: utilities_compatibility
#
# Unit tests for compatibility shims (C++ polyfills).
#   ctest -L compatibility  # Run all compatibility tests
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (4 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/compat_endian_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat_byteswap_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat_span_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat_concurrency_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestUtilitiesCompatibility
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  utilities_compatibility
  LABELS
  utilities
  compatibility
)
