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

# CPU tests: separate binary, no _cuda linkage -> runs on any host.
apex_add_gtest(
  TARGET
  TestSystemCoreSchedulable
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_schedulable
  utilities_concurrency
  fmt::fmt
  LABELS
  system
  schedulable
)

# CUDA tests: separate binary linking the _cuda sidecar (keeps the CPU tests
# above driver-free). Labeled cuda so the test runner can exclude them where
# no GPU is present.
if (CUDAToolkit_FOUND AND TARGET system_core_schedulable_cuda)
  apex_add_gtest(
    TARGET
    TestSystemCoreSchedulableCuda
    SOURCES
    ${CUDA_TEST_SOURCES}
    LINK
    system_core_schedulable
    system_core_schedulable_cuda
    utilities_concurrency
    fmt::fmt
    LABELS
    system
    schedulable
    cuda
  )
  set_target_properties(
    TestSystemCoreSchedulableCuda PROPERTIES CUDA_SEPARABLE_COMPILATION ON
                                             CUDA_RESOLVE_DEVICE_SYMBOLS ON
  )
endif ()
