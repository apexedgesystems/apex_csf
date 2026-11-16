/**
 * @file PoolBase.cpp
 * @brief Worker start-up, joining and error collection shared by the thread pools.
 */

#include "src/utilities/concurrency/inc/PoolBase.hpp"
#include "src/utilities/concurrency/inc/Latch.hpp"
#include "src/utilities/concurrency/inc/ThreadConfig.hpp"

#include <array>
#include <cstdio>
#include <system_error>
#include <thread>

namespace apex {
namespace concurrency {

/* ----------------------------- PoolBase Methods ----------------------------- */

bool PoolBase::spawnWorkers(std::size_t numThreads, const PoolConfig& config,
                            const char* namePrefix, WorkerEntry entry, void* pool) {
  // The workers inherit the blocked process signals; the scope hands the
  // constructing thread its own mask back when the spawn is over, success
  // or not.
  const WorkerSignalScope SIGNALS;
  // Each worker counts down once its configuration is applied and never
  // touches the latch again. This thread parks on it rather than yielding,
  // so a worker configured below its priority, on the same CPU, still gets
  // to run.
  Latch configured(numThreads);
  workers_.reserve(numThreads);
  std::size_t started = 0;
  try {
    for (; started < numThreads; ++started) {
      workers_.emplace_back([this, config, namePrefix, entry, pool, started, &configured]() {
        // The worker's name: the prefix and its index (15 characters and a terminator on Linux).
        std::array<char, 16> name{};
        std::snprintf(name.data(), name.size(), "%s%zu", namePrefix, started);
        if (!applyPoolConfig(config, name.data())) {
          configFailures_.fetch_add(1, std::memory_order_acq_rel);
        }
        configured.countDown();
        entry(pool);
      });
    }
  } catch (const std::system_error&) {
    // A thread could not start. The arrivals that will never come are counted
    // here, so the wait below still ends once every worker that did start has
    // applied its configuration and let go of the latch.
    configured.countDown(numThreads - started);
  }
  configured.wait();
  return started == numThreads;
}

void PoolBase::joinWorkers() noexcept {
  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  workers_.clear();
}

std::queue<TaskError> PoolBase::collectErrors() {
  const std::lock_guard<std::mutex> LOCK(errorMutex_);
  std::queue<TaskError> out = std::move(errorQueue_);
  errorQueue_ = {};
  hasErrors_.value.store(false, std::memory_order_release);
  return out;
}

void PoolBase::recordFailure(const char* label, std::uint8_t code) {
  const std::lock_guard<std::mutex> LOCK(errorMutex_);
  errorQueue_.push(TaskError{label, code, PoolStatus::TASK_FAILED});
  hasErrors_.value.store(true, std::memory_order_release);
}

} // namespace concurrency
} // namespace apex
