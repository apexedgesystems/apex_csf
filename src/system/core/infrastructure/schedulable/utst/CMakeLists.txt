# ==============================================================================
# Unit Tests: system_core_schedulable
#
# Tests SchedulableTaskBase (construction, label, callable), SchedulableTask
# (execute, delegate binding), and SchedulableTaskCUDA (stream/event lifecycle,
# completion tracking).
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources (3 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/SchedulableTask_uTest.cpp"
                      "${CMAKE_CURRENT_SOURCE_DIR}/SchedulableTaskBase_uTest.cpp"
)

set(CUDA_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/SchedulableTaskCUDA_uTest.cu")

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreSchedulable
  SOURCES
  ${UNIT_TEST_SOURCES}
  CUDA
  ${CUDA_TEST_SOURCES}
  LINK
  system_core_schedulable
  system_core_schedulable_cuda
  utilities_concurrency
  fmt::fmt
  LABELS
  system
  schedulable
)
