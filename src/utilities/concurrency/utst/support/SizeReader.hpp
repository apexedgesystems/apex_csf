#ifndef APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_SIZE_READER_HPP
#define APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_SIZE_READER_HPP
/**
 * @file SizeReader.hpp
 * @brief A third thread that samples a queue's sizeApprox() while a producer
 *        and a consumer run, for the queues whose size query takes two index
 *        reads that are not one instant.
 */

#include "src/utilities/concurrency/utst/support/Crew.hpp"
#include "src/utilities/concurrency/utst/support/Waiting.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>

namespace apex {
namespace concurrency {
namespace test {

/// What the third reader saw.
struct SizeReadings {
  bool sampled{false};     ///< The reader was sampling before the exchange began.
  bool finished{false};    ///< Every value arrived and the three threads ended within the bound.
  std::size_t largest{0};  ///< The largest sizeApprox() read.
  std::size_t capacity{0}; ///< The queue's capacity.
};

/// Pass `items` ints through a `Queue<int>` of `capacity` from one producer to one consumer
/// while a third thread reads sizeApprox(). The exchange starts only once the reader is
/// sampling; the reader samples in bursts and yields between them, and the producer and consumer
/// yield when refused, so the three threads make progress on one core. The queue and the
/// threads belong to a Crew: an exchange that never completes ends the call with `finished`
/// false. This meets a split read of the two indices by chance, not by construction.
template <template <class> class Queue>
SizeReadings readSizeFromAThirdThread(std::size_t capacity, int items) {
  struct Exchange : CrewState {
    explicit Exchange(std::size_t cap) : queue(cap) {}
    Queue<int> queue;
    std::atomic<bool> done{false};
    std::atomic<bool> sampling{false};
    std::atomic<std::size_t> largest{0};
  };
  Crew<Exchange> crew(capacity);
  SizeReadings out;
  out.capacity = crew->queue.capacity();

  crew.start([](Exchange& state) {
    std::size_t largest = 0;
    while (!state.done.load(std::memory_order_acquire) &&
           !state.abandoned.load(std::memory_order_acquire)) {
      for (int sample = 0; sample < 256; ++sample) {
        const std::size_t SIZE = state.queue.sizeApprox();
        if (SIZE > largest) {
          largest = SIZE;
        }
      }
      state.largest.store(largest, std::memory_order_release);
      state.sampling.store(true, std::memory_order_release);
      std::this_thread::yield();
    }
  });
  out.sampled = settles([&]() { return crew->sampling.load(std::memory_order_acquire); });
  if (!out.sampled) {
    crew->done.store(true, std::memory_order_release);
    (void)crew.finish();
    return out;
  }

  crew.start([items](Exchange& state) {
    int value = 0;
    for (int received = 0; received < items && !state.abandoned.load(std::memory_order_acquire);) {
      if (state.queue.tryPop(value)) {
        ++received;
      } else {
        std::this_thread::yield();
      }
    }
    state.done.store(true, std::memory_order_release);
  });
  crew.start([items](Exchange& state) {
    for (int i = 0; i < items && !state.abandoned.load(std::memory_order_acquire);) {
      if (state.queue.tryPush(i)) {
        ++i;
      } else {
        std::this_thread::yield();
      }
    }
  });
  out.finished = crew.finish(std::chrono::seconds(20));
  out.largest = crew->largest.load(std::memory_order_acquire);
  return out;
}

} // namespace test
} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_SIZE_READER_HPP
