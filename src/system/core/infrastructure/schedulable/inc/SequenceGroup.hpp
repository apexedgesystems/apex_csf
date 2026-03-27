#ifndef APEX_SYSTEM_CORE_SCHEDULABLE_SEQUENCEGROUP_HPP
#define APEX_SYSTEM_CORE_SCHEDULABLE_SEQUENCEGROUP_HPP
/**
 * @file SequenceGroup.hpp
 * @brief Phase-based task sequencing for intra-frame coordination.
 *
 * SequenceGroup enables strict ordering of tasks within a frame:
 *   - Tasks at the same phase run in parallel
 *   - Tasks at higher phases wait for lower phases to complete
 *   - Counter-based coordination with hybrid spin/park wait
 *
 * Design:
 *   SequenceGroup maintains an internal registry mapping tasks to their
 *   sequencing info. When tasks are added to the scheduler, the scheduler
 *   queries this registry to populate TaskEntry with sequencing config.
 *
 * RT-Safety:
 *   - Construction allocates (not RT-safe, do at init time)
 *   - addTask() is RT-safe after construction
 *   - Wait/advance in worker threads (hybrid spin/park)
 *
 * Example:
 * @code
 * SequenceGroup seq(4);  // 4 total sequenced tasks
 * seq.addTask(taskPre1, 1);  // phase 1
 * seq.addTask(taskPre2, 1);  // phase 1 (parallel with pre1)
 * seq.addTask(taskStep, 3);  // phase 3 (waits for 2 tasks at phase 1)
 * seq.addTask(taskPost, 4);  // phase 4 (waits for step)
 *
 * scheduler.addTask(taskPre1, config, &seq);
 * scheduler.addTask(taskPre2, config, &seq);
 * scheduler.addTask(taskStep, config, &seq);
 * scheduler.addTask(taskPost, config, &seq);
 * @endcode
 */

#include "src/utilities/compatibility/inc/compat_concurrency.hpp"
#include "src/utilities/helpers/inc/Cpu.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace system_core {
namespace schedulable {

// Forward declaration
class SchedulableTask;

/* ----------------------------- SeqInfo ----------------------------- */

/**
 * @struct SeqInfo
 * @brief Sequencing info for a single task (stored in registry).
 */
struct SeqInfo {
  int phase{0};    ///< Phase this task waits for.
  int maxPhase{0}; ///< Maximum phase (for counter wrap).
};

/* ----------------------------- SequenceGroup ----------------------------- */

/**
 * @class SequenceGroup
 * @brief Manages phase-based task sequencing with internal registry.
 *
 * Each task increments the counter when done. Tasks wait for counter >= phase.
 * Phase numbers must account for task count at each phase:
 *   - 2 tasks at phase 1 -> next phase is 3 (after both increment)
 *   - 1 task at phase 3 -> next phase is 4
 */
class SequenceGroup {
public:
  /**
   * @brief Construct a sequence group.
   * @param maxPhase Maximum phase value (= total sequenced task count).
   *
   * @note Allocates shared counter; perform at init time, not in RT path.
   */
  explicit SequenceGroup(int maxPhase) noexcept
      : counter_(std::make_shared<std::atomic<int>>(1)), maxPhase_(maxPhase) {}

  /**
   * @brief Register a task with this sequence at a specific phase.
   * @param task Task to register.
   * @param phase Phase number (counter value to wait for).
   *
   * Phase numbers should be calculated as:
   *   1 for first task(s), then +1 for each prior task in sequence.
   */
  void addTask(SchedulableTask& task, int phase) noexcept {
    registry_[&task] = SeqInfo{phase, maxPhase_};
  }

  /**
   * @brief Get sequencing info for a task.
   * @param task Task to look up.
   * @return Pointer to SeqInfo, or nullptr if task not in sequence.
   */
  [[nodiscard]] const SeqInfo* getSeqInfo(const SchedulableTask* task) const noexcept {
    auto it = registry_.find(task);
    return (it != registry_.end()) ? &it->second : nullptr;
  }

  /**
   * @brief Get the shared counter.
   * @return Shared pointer to the atomic counter.
   */
  [[nodiscard]] std::shared_ptr<std::atomic<int>> counter() const noexcept { return counter_; }

  /**
   * @brief Get the maximum phase number.
   * @return Max phase (= total task count).
   */
  [[nodiscard]] int maxPhase() const noexcept { return maxPhase_; }

  /**
   * @brief Reset the counter to initial state (phase 1).
   */
  void reset() noexcept { counter_->store(1, std::memory_order_release); }

private:
  std::shared_ptr<std::atomic<int>> counter_;
  int maxPhase_;
  std::unordered_map<const SchedulableTask*, SeqInfo> registry_;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Wait until counter reaches expected phase.
 * @param counter Shared atomic counter.
 * @param expectedPhase Phase to wait for.
 *
 * Uses hybrid wait: short spin with exponential backoff, then park.
 * Force-inlined for predictable hot path performance.
 */
inline void waitForPhase(std::atomic<int>& counter, int expectedPhase) noexcept {
  // Fast path: check once before spinning
  if (counter.load(std::memory_order_acquire) >= expectedPhase) {
    return;
  }

  // Hybrid wait: spin first, then park
  // Higher spin budget (256) works better to avoid parking overhead
  constexpr unsigned SPIN_BUDGET = 256;
  apex::helpers::cpu::ExponentialBackoff bk;

  for (unsigned i = 0; i < SPIN_BUDGET; ++i) {
    if (counter.load(std::memory_order_acquire) >= expectedPhase) {
      return;
    }
    bk.spinOnce();
  }

  // Park until ready
  for (;;) {
    const int cur = counter.load(std::memory_order_acquire);
    if (cur >= expectedPhase) {
      break;
    }
    apex::compat::atom::waitEq(counter, cur);
  }
}

/**
 * @brief Advance counter after task completion.
 * @param counter Shared atomic counter.
 * @param maxPhase Maximum phase (for counter wrap).
 *
 * Increments counter and notifies waiters. Wraps to 1 after maxPhase.
 * Uses CAS loop to ensure atomic increment-with-wrap.
 */
inline void advancePhase(std::atomic<int>& counter, int maxPhase) noexcept {
  // Atomic increment with wrap using CAS loop
  int cur = counter.load(std::memory_order_acquire);
  int next = 0;
  do {
    next = (cur >= maxPhase) ? 1 : (cur + 1);
  } while (!counter.compare_exchange_weak(cur, next, std::memory_order_acq_rel,
                                          std::memory_order_acquire));

  apex::compat::atom::notifyAll(counter);
}

} // namespace schedulable
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SCHEDULABLE_SEQUENCEGROUP_HPP
