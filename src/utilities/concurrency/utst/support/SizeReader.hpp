#ifndef APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_SIZE_READER_HPP
#define APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_SIZE_READER_HPP
/**
 * @file SizeReader.hpp
 * @brief A third thread that samples a queue's sizeApprox() while a producer
 *        and a consumer run, for the queues whose size query takes two index
 *        reads that are not one instant.
 */

#include "src/utilities/concurrency/utst/support/Waiting.hpp"

#include <atomic>
#include <cstddef>
#include <optional>
#include <thread>

namespace apex {
namespace concurrency {
namespace test {

/// The largest sizeApprox() a third thread reads while `items` ints pass through `queue` from
/// one producer to one consumer. Empty when the reader never sampled. The exchange starts only
/// once the reader is sampling; the reader samples in bursts and yields between them, and the
/// producer and consumer yield when refused, so the three threads make progress on one core.
/// This meets a split read of the two indices by chance, not by construction.
template <class Queue>
std::optional<std::size_t> largestSizeSeenByAThirdReader(Queue& queue, int items) {
  std::atomic<bool> done{false};
  std::atomic<bool> sampling{false};
  std::atomic<std::size_t> largestSeen{0};

  std::thread reader([&]() {
    std::size_t largest = 0;
    while (!done.load(std::memory_order_acquire)) {
      for (int sample = 0; sample < 256; ++sample) {
        const std::size_t SIZE = queue.sizeApprox();
        if (SIZE > largest) {
          largest = SIZE;
        }
      }
      sampling.store(true, std::memory_order_release);
      std::this_thread::yield();
    }
    largestSeen.store(largest, std::memory_order_release);
  });

  const bool READER_SAMPLING = settles([&]() { return sampling.load(std::memory_order_acquire); });
  if (READER_SAMPLING) {
    std::thread consumer([&]() {
      int value = 0;
      for (int received = 0; received < items;) {
        if (queue.tryPop(value)) {
          ++received;
        } else {
          std::this_thread::yield();
        }
      }
    });
    for (int i = 0; i < items;) {
      if (queue.tryPush(i)) {
        ++i;
      } else {
        std::this_thread::yield();
      }
    }
    consumer.join();
  }
  done.store(true, std::memory_order_release);
  reader.join();

  if (!READER_SAMPLING) {
    return std::nullopt;
  }
  return largestSeen.load(std::memory_order_acquire);
}

} // namespace test
} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_SIZE_READER_HPP
