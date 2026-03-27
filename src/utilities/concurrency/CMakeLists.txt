# ==============================================================================
# Module: utilities_concurrency
#
# Thread-safe concurrency primitives for real-time systems.
#
# Components:
#   ThreadPool         - RT-optimized thread pool with zero-allocation tasks
#   ThreadPoolLockFree - Lock-free thread pool with bounded MPMC queue
#   LockFreeQueue      - Bounded MPMC queue (Vyukov algorithm)
#   SPSCQueue          - Lock-free single-producer/single-consumer queue
#   RingBuffer         - Single-threaded queue (zero atomics, maximum speed)
#   Semaphore          - Counting semaphore with atomic fast-path
#   SpinLock           - Busy-wait lock with PAUSE hints
#   Latch              - One-shot countdown synchronization (C++20 backport)
#   Barrier            - Reusable multi-phase synchronization (C++20 backport)
#   DelegateU8         - Allocation-free callable wrapper
#   AlignCl            - Cacheline-alignment helper
# ==============================================================================

include(apex/All)
apex_module(utilities_concurrency)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/ThreadPool.cpp" "${SRC_DIR}/ThreadPoolLockFree.cpp"
                "${SRC_DIR}/Semaphore.cpp" "${SRC_DIR}/Latch.cpp" "${SRC_DIR}/Barrier.cpp"
)

# ------------------------------------------------------------------------------
# Library
# ------------------------------------------------------------------------------

apex_add_library(
  NAME
  ${LIB_NAME}
  TYPE
  SHARED
  INC
  "${INC_DIR}"
  SRC
  ${CPP_SOURCES}
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst tst)
