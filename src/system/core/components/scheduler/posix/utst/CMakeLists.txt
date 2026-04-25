# ==============================================================================
# Unit Tests: system_core_scheduler
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Test Sources (5)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/SchedulerData_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SchedulerStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TaskBuilder_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SchedulableTask_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SchedulerOverhead_uTest.cpp"
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
)
