# ==============================================================================
# Unit Tests: utilities_concurrency
#
# Unit tests for concurrency primitives.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (14 files, plus the thread-launch seam under support/)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/ThreadPool_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ThreadPoolLockFree_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LockFreeQueue_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SPSCQueue_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/RingBuffer_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/RingIndex_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/RingStorage_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Semaphore_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SpinLock_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Latch_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Barrier_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Delegate_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Cache_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ThreadConfig_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/support/ThreadStartFault.cpp"
)

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestUtilitiesConcurrency
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  utilities_concurrency
  ${CMAKE_DL_LIBS}
  LABELS
  utilities
  concurrency
  # These four assert an upper bound on elapsed wall-clock time, which host
  # load stretches when the suite runs in parallel. Timing-marked so they run
  # serially.
  TIMING_TESTS
  SemaphoreTest.TryAcquireForSucceedsImmediately
  SemaphoreTest.TryAcquireForSucceedsOnRelease
  LatchTest.WaitOnTriggeredReturnsImmediately
  LatchTest.WaitForSucceedsOnTrigger
)
