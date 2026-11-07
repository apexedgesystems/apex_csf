/**
 * @file Probe.cpp
 * @brief Compile probe: the surface a consumer of the hosted concurrency
 *        library reaches, built at each dialect lib.manifest declares with
 *        -Werror. The manifest's headers line gives every public header its
 *        own generated TU in the same build, so a header that compiles only
 *        through another's transitive includes fails there; this TU checks
 *        that the API is usable at the declared dialects.
 */

#include "src/utilities/concurrency/inc/Barrier.hpp"
#include "src/utilities/concurrency/inc/Cache.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"
#include "src/utilities/concurrency/inc/Latch.hpp"
#include "src/utilities/concurrency/inc/LockFreeQueue.hpp"
#include "src/utilities/concurrency/inc/RingBuffer.hpp"
#include "src/utilities/concurrency/inc/SPSCQueue.hpp"
#include "src/utilities/concurrency/inc/Semaphore.hpp"
#include "src/utilities/concurrency/inc/SpinLock.hpp"
#include "src/utilities/concurrency/inc/ThreadConfig.hpp"
#include "src/utilities/concurrency/inc/ThreadPool.hpp"
#include "src/utilities/concurrency/inc/ThreadPoolLockFree.hpp"

#include <chrono>
#include <cstdint>

// The hosted library parks on std::atomic::wait (Semaphore.cpp), a C++20
// facility the probe's headers do not reach, so a manifest declaring an older
// dialect fails on this line rather than silently compiling the headers. The
// library's own sources are built at the project dialect; this line states
// the floor, it does not compile them at it.
static_assert(__cplusplus >= 202002L, "utilities_concurrency needs C++20");

using namespace apex::concurrency;

namespace {

std::uint8_t noop(void*) noexcept { return 0; }

} // namespace

std::size_t probe() {
  Barrier barrier(2);
  barrier.arriveAndDrop();
  Latch latch(1);
  latch.countDown();
  LockFreeQueue<int> queue(4);
  int popped = 0;
  const bool MOVED = queue.tryPush(1) && queue.tryPop(popped);
  SpinLock lock;
  const bool HELD = lock.tryLock();
  lock.unlock();
  ThreadPool pool(1);
  ThreadPoolLockFree lockFree(1, 4);
  const PoolStatus STATUS = lockFree.tryEnqueue("probe", DelegateU8{&noop, nullptr});
  WorkerSignalScope signals;
  return barrier.expected() + latch.count() + static_cast<std::size_t>(MOVED) +
         static_cast<std::size_t>(HELD) + pool.workerCount() + lockFree.workerConfigFailures() +
         static_cast<std::size_t>(STATUS == PoolStatus::SUCCESS) +
         static_cast<std::size_t>(latch.waitFor(std::chrono::microseconds(0))) +
         static_cast<std::size_t>(lock.isLocked());
}
