# ==============================================================================
# Unit Tests: system_core_logs
#
# Tests file-backed logging operations including:
#   - LogBase open, write, rotate, size, flush, status
#   - SystemLog severity filtering, verbosity, async/sync modes
#   - AsyncLogBackend queue lifecycle, enqueue, flush, overflow
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/LogBase_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SystemLog_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/AsyncLogBackend_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreLogs
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_logs
  fmt::fmt
  LABELS
  system
  core
  logs
)
