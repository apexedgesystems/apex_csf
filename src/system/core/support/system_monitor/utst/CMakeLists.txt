# ==============================================================================
# Unit Tests: system_core_support_system_monitor (1 file)
#
# Tests run on the host and exercise real /proc and /sys reads.
# They verify invariants (e.g., core count > 0) rather than exact values.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/SystemMonitor_uTest.cpp")

# ------------------------------------------------------------------------------
# Test Executable
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreSupport_SystemMonitor
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_support_system_monitor
  fmt::fmt
  LABELS
  system
  support
)
