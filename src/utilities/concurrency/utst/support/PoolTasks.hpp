#ifndef APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_POOL_TASKS_HPP
#define APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_POOL_TASKS_HPP
/**
 * @file PoolTasks.hpp
 * @brief Tasks and scenarios the two thread pools' tests share: a counting
 *        task, tasks that hold a worker, and a drain scenario that stops a
 *        pool while its only worker is held.
 */

#include "src/utilities/concurrency/inc/ThreadPool.hpp"
#include "src/utilities/concurrency/utst/support/Crew.hpp"
#include "src/utilities/concurrency/utst/support/Waiting.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <thread>
#include <utility>

namespace apex {
namespace concurrency {
namespace test {

/// A task that counts its runs: `ctx` is a std::atomic<std::uint32_t>.
inline std::uint8_t countRun(void* ctx) noexcept {
  static_cast<std::atomic<std::uint32_t>*>(ctx)->fetch_add(1, std::memory_order_acq_rel);
  return 0;
}

/// A task that announces it is holding its worker and keeps it until the gate opens.
struct Gate {
  std::atomic<int> holding{0};
  std::atomic<bool> open{false};

  static std::uint8_t hold(void* ctx) noexcept {
    auto* gate = static_cast<Gate*>(ctx);
    gate->holding.fetch_add(1, std::memory_order_acq_rel);
    while (!gate->open.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    return 0;
  }
};

/// A task that holds its worker until the pool refuses work, so the worker is released only
/// after the pool has begun to stop. It finds that out by offering the pool a counting task
/// every millisecond; each one the pool accepts is one more task the pool owes a run.
template <class Pool> struct StopGate {
  Pool* pool{nullptr};
  std::atomic<std::uint32_t>* ran{nullptr};
  std::atomic<int> holding{0};
  std::atomic<std::uint32_t> accepted{0};

  static std::uint8_t holdUntilStopped(void* ctx) noexcept {
    auto* gate = static_cast<StopGate*>(ctx);
    gate->holding.fetch_add(1, std::memory_order_acq_rel);
    for (;;) {
      const PoolStatus STATUS = gate->pool->tryEnqueue("probe", {&countRun, gate->ran});
      if (STATUS == PoolStatus::POOL_STOPPED) {
        return 0;
      }
      if (STATUS == PoolStatus::SUCCESS) {
        gate->accepted.fetch_add(1, std::memory_order_acq_rel);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
};

/// What a drain found. `finished` is false when the stop did not return within the bound.
struct DrainOutcome {
  bool workerHeld{false};         ///< The worker was inside the gate task before the stop.
  bool finished{false};           ///< shutdown() or the destructor returned.
  std::uint32_t ranBeforeStop{0}; ///< Counted tasks run while the only worker was held.
  std::uint32_t accepted{0};      ///< Counted tasks the pool accepted, the gate's probes included.
  std::uint32_t ran{0};           ///< Counted tasks run by the time the stop returned.
  bool validAfter{true};          ///< isValid() after shutdown() (shutdown drains only).
  PoolStatus enqueueAfter{PoolStatus::SUCCESS}; ///< tryEnqueue() after a second shutdown().
};

/// Stop a one-worker pool while its worker is held inside a task and counted tasks wait behind
/// it. The worker is released only once the pool refuses work, so every waiting task is still
/// queued at the moment the stop begins: a pool that stops without running its queue leaves
/// `ran` short of `accepted` on every run. The stop runs on a Crew thread, by shutdown() or by
/// destroying the pool, and the pool is the last member so it is destroyed before what its
/// tasks touch.
template <class Pool, class... PoolArgs>
DrainOutcome drainHeldPool(bool byDestructor, std::uint32_t queued, PoolArgs... poolArgs) {
  struct Scenario : CrewState {
    explicit Scenario(PoolArgs... args) {
      pool.emplace(args...);
      gate.pool = &*pool;
      gate.ran = &ran;
    }
    std::atomic<std::uint32_t> ran{0};
    StopGate<Pool> gate;
    std::optional<Pool> pool;
  };
  Crew<Scenario> crew(poolArgs...);
  DrainOutcome out;

  if (crew->pool->tryEnqueue("gate", {&StopGate<Pool>::holdUntilStopped, &crew->gate}) !=
      PoolStatus::SUCCESS) {
    return out;
  }
  out.workerHeld = settles([&]() { return crew->gate.holding.load() == 1; });
  if (!out.workerHeld) {
    return out;
  }
  std::uint32_t queuedHere = 0;
  for (std::uint32_t i = 0; i < queued; ++i) {
    if (crew->pool->tryEnqueue("queued", {&countRun, &crew->ran}) == PoolStatus::SUCCESS) {
      ++queuedHere;
    }
  }
  out.ranBeforeStop = crew->ran.load();

  if (byDestructor) {
    crew.start([](Scenario& scenario) { scenario.pool.reset(); });
  } else {
    crew.start([](Scenario& scenario) { scenario.pool->shutdown(); });
  }
  out.finished = crew.finish();
  if (!out.finished) {
    return out;
  }
  out.accepted = queuedHere + crew->gate.accepted.load();
  out.ran = crew->ran.load();
  if (!byDestructor) {
    out.validAfter = crew->pool->isValid();
    crew->pool->shutdown();
    out.enqueueAfter = crew->pool->tryEnqueue("late", {&countRun, &crew->ran});
  }
  return out;
}

/// What a pool did with a null delegate queued between two counting tasks.
struct NullTaskOutcome {
  bool finished{false};      ///< The submissions and shutdown() returned within the bound.
  std::uint32_t accepted{0}; ///< Submissions the pool accepted, of three.
  std::uint32_t ran{0};      ///< Counting tasks run, of two.
  bool hasErrors{true};      ///< hasErrors() after shutdown().
  std::size_t errors{1};     ///< Errors collected after shutdown().
};

/// Queue a counting task, a null delegate and a second counting task on a one-worker pool and
/// shut it down, on a Crew thread so a shutdown() that never returns fails the case.
template <class Pool, class... PoolArgs> NullTaskOutcome runAroundNullDelegate(PoolArgs... args) {
  struct Scenario : CrewState {
    explicit Scenario(PoolArgs... poolArgs) : pool(poolArgs...) {}
    std::atomic<std::uint32_t> ran{0};
    std::atomic<std::uint32_t> accepted{0};
    Pool pool;
  };
  Crew<Scenario> crew(args...);
  crew.start([](Scenario& scenario) {
    const DelegateU8 TASKS[] = {
        {&countRun, &scenario.ran}, {nullptr, nullptr}, {&countRun, &scenario.ran}};
    for (const DelegateU8& task : TASKS) {
      if (scenario.pool.tryEnqueue("task", task) == PoolStatus::SUCCESS) {
        scenario.accepted.fetch_add(1, std::memory_order_acq_rel);
      }
    }
    scenario.pool.shutdown();
  });
  NullTaskOutcome out;
  out.finished = crew.finish();
  if (!out.finished) {
    return out;
  }
  out.accepted = crew->accepted.load();
  out.ran = crew->ran.load();
  out.hasErrors = crew->pool.hasErrors();
  out.errors = crew->pool.collectErrors().size();
  return out;
}

} // namespace test
} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_POOL_TASKS_HPP
