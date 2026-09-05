# ==============================================================================
# Unit Tests: system_core_scheduler
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Test Sources (5)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/SchedulerCtxPool_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SchedulerData_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SchedulerStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SchedulerOverhead_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SchedulerShutdown_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SchedulerPreflight_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SchedulerTaskStats_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SchedulerCensus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SchedulerRequirements_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreScheduler
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_scheduler
  system_core_logs
  utilities_concurrency
  fmt::fmt
  LABELS
  system
  core
  scheduler
  # The ctx-burst test asserts zero fallback allocations during a dense
  # tick, which holds only when pool workers drain at dispatch pace --
  # under parallel suite load the box can starve the workers and flake
  # the assertion. Timing-marked so it runs serially.
  TIMING_TESTS
  SchedulerCtxBurstTest.DenseTickDispatchesAllTasksWithoutFallback
)
