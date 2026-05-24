# ==============================================================================
# Performance Tests: system_core_time_server
#
# TimeServer hot-path overhead: per-frame tick (with and without PPS edges),
# the ATS time-provider trampoline, UTC interpolation, command handlers, and
# the RELAY-mode anchor.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/TimeServer_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET
  TimeServer_PTEST
  SOURCES
  ${PERF_TEST_SOURCES}
  LINK
  system_core_time_server
  hal_mock
  fmt::fmt
)
