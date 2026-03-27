#ifndef APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERWAITPOLICY_HPP
#define APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERWAITPOLICY_HPP
/**
 * @file SchedulerWaitPolicy.hpp
 * @brief Pluggable wait/notify strategies for intra-frame task ordering.
 *
 * Provides wait policies for SequenceGroup-based task coordination:
 *   - SpinWaitPolicy: Bounded spin/backoff (lowest latency, highest CPU)
 *   - AtomicWaitPolicy: Park via atomic wait/notify (lowest CPU, higher latency)
 *   - HybridWaitPolicy: Short spin then park (default, balanced)
 *
 * RT-Safety:
 *   - All wait methods are bounded (won't spin forever)
 *   - No heap allocations in wait/notify paths
 *   - Suitable for hard real-time systems
 */

#include "src/utilities/compatibility/inc/compat_concurrency.hpp"
#include "src/utilities/helpers/inc/Cpu.hpp"

#include <cstdint>

#include <atomic>

namespace system_core {
namespace scheduler {

/* ----------------------------- WaitPolicy Interface ----------------------------- */

/**
 * @class WaitPolicy
 * @brief Interface for intra-frame wait/notify strategies.
 *
 * Tasks use WaitPolicy to coordinate execution order within a frame.
 * Different policies trade off latency vs CPU usage.
 */
class WaitPolicy {
public:
  virtual ~WaitPolicy() noexcept = default;

  /**
   * @brief Block until counter >= expected.
   * @param counter Atomic counter to watch.
   * @param expected Threshold value to wait for.
   */
  virtual void waitReady(std::atomic<int>& counter, int expected) noexcept = 0;

  /**
   * @brief Notify waiters that counter has advanced.
   * @param counter Atomic counter that was incremented.
   */
  virtual void notifyAdvance(std::atomic<int>& counter) noexcept = 0;
};

/* ----------------------------- SpinWaitPolicy ----------------------------- */

/**
 * @class SpinWaitPolicy
 * @brief Bounded spin with exponential backoff.
 *
 * Best for sub-microsecond phase transitions where CPU usage is acceptable.
 * Uses exponential backoff to reduce contention while maintaining low latency.
 */
class SpinWaitPolicy final : public WaitPolicy {
public:
  explicit SpinWaitPolicy(unsigned maxSpins = (1u << 12)) noexcept : maxSpins_(maxSpins) {}

  void waitReady(std::atomic<int>& counter, int expected) noexcept override {
    apex::helpers::cpu::ExponentialBackoff bk;
    unsigned spins = 0;
    for (;;) {
      if (counter.load(std::memory_order_acquire) >= expected) {
        break;
      }
      bk.spinOnce();
      if (++spins >= maxSpins_) {
        break; // Safety cap
      }
    }
  }

  void notifyAdvance(std::atomic<int>& /*counter*/) noexcept override {
    // Spinners observe via loads, no explicit notification needed
  }

private:
  unsigned maxSpins_;
};

/* ----------------------------- AtomicWaitPolicy ----------------------------- */

/**
 * @class AtomicWaitPolicy
 * @brief Park via atomic wait/notify.
 *
 * Best for longer waits where CPU efficiency matters more than latency.
 * Uses OS-level wait primitives for efficient blocking.
 */
class AtomicWaitPolicy final : public WaitPolicy {
public:
  void waitReady(std::atomic<int>& counter, int expected) noexcept override {
    for (;;) {
      const int CUR = counter.load(std::memory_order_acquire);
      if (CUR >= expected) {
        break;
      }
      apex::compat::atom::waitEq(counter, CUR);
    }
  }

  void notifyAdvance(std::atomic<int>& counter) noexcept override {
    apex::compat::atom::notifyAll(counter);
  }
};

/* ----------------------------- HybridWaitPolicy ----------------------------- */

/**
 * @class HybridWaitPolicy
 * @brief Short spin followed by park (default policy).
 *
 * Balanced approach: tries spinning first for low-latency cases,
 * falls back to parking if the wait is longer. Good default choice.
 */
class HybridWaitPolicy final : public WaitPolicy {
public:
  explicit HybridWaitPolicy(unsigned spinBudget = 256) noexcept : spinBudget_(spinBudget) {}

  void waitReady(std::atomic<int>& counter, int expected) noexcept override {
    // Phase 1: Spin with backoff
    apex::helpers::cpu::ExponentialBackoff bk;
    for (unsigned i = 0; i < spinBudget_; ++i) {
      if (counter.load(std::memory_order_acquire) >= expected) {
        return;
      }
      bk.spinOnce();
    }

    // Phase 2: Park
    for (;;) {
      const int CUR = counter.load(std::memory_order_acquire);
      if (CUR >= expected) {
        break;
      }
      apex::compat::atom::waitEq(counter, CUR);
    }
  }

  void notifyAdvance(std::atomic<int>& counter) noexcept override {
    apex::compat::atom::notifyAll(counter);
  }

private:
  unsigned spinBudget_;
};

} // namespace scheduler
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERWAITPOLICY_HPP
