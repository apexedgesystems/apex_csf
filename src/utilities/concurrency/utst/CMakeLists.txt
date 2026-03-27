# ==============================================================================
# Unit Tests: utilities_concurrency
#
# Unit tests for concurrency primitives.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (11 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/ThreadPool_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ThreadPoolLockFree_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LockFreeQueue_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SPSCQueue_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/RingBuffer_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Semaphore_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SpinLock_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Latch_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Barrier_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Delegate_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Cache_uTest.cpp"
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
  LABELS
  utilities
  concurrency
)
