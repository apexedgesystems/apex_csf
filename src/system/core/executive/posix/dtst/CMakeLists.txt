# ==============================================================================
# Development Tests: system_core_executive
#
# Full-boot timing-integrity tests: clock grid repayment, task backlog
# drain, and the >1 s resync policy, exercised on a real executive via
# pause/resume. Wall-clock sensitive -- run on an otherwise-idle host.
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(DEV_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ExecutiveTiming_dTest.cpp")

# ------------------------------------------------------------------------------
# Dev Test Target (manual execution only)
# ------------------------------------------------------------------------------

apex_add_devtest(
  TARGET
  ExecutiveTiming_Dev
  SOURCES
  ${DEV_TEST_SOURCES}
  LINK
  system_core_executive
  # The executive embeds these by value in its header, so targets that
  # construct one link them directly (the library keeps them private).
  system_core_scheduler
  system_core_filesystem
  fmt::fmt
)
