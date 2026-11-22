# ==============================================================================
# Performance Tests: system_core_executive
#
# Delivered-cadence baseline for the executive clock: period fidelity,
# jitter tail, and cumulative drift measured from outside via health
# packet cycle edges. Task-loop baseline: the task thread's CPU time per
# delivered cycle, with nothing scheduled and with one registered
# component's task dispatched every cycle.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ExecutiveCadence_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET
  ExecutiveCadence_PTEST
  SOURCES
  ${PERF_TEST_SOURCES}
  LINK
  system_core_executive
  # The executive embeds these by value in its header, so targets that
  # construct one link them directly (the library keeps them private).
  system_core_scheduler
  system_core_filesystem
  fmt::fmt
)

apex_add_ptest(
  TARGET
  ExecutiveTaskLoop_PTEST
  SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/ExecutiveTaskLoop_pTest.cpp"
  LINK
  system_core_executive
  system_core_scheduler
  system_core_filesystem
  system_core_system_component
  system_core_logs
  fmt::fmt
)
