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
 *   - Construction and addTask() are config-time (addTask inserts into
 *     the registry map); keep both out of RT paths.
 *   - Wait/advance in worker threads (hybrid spin/park) are the RT
 *     surface.
 *
 * Lifetime: the group owns the phase counter. Scheduler entries hold a
 * pointer to it, so the group must outlive every task registered with
 * the scheduler against it.
 *
 * Frame contract: a sequenced chain must complete within its period.
 * The counter wraps on the final advance of a cycle; if the next
 * period's phase-1 task can start while this period's tail phase still
 * waits (an overrun), the early advance can wrap the counter beneath
 * the tail waiter, which then completes in the following cycle --
 * delayed, never corrupt.
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
#include <unordered_map>

namespace system_core {
namespace schedulable {

// Forward declaration
class SchedulableTask;

/// Terminal counter value stored by the scheduler at shutdown. Never a
/// valid phase (phases are >= 1), and negative so no waiter's
/// `counter >= phase` check can pass on it: waiters wake on the value
/// change and exit via their abort flag.
inline constexpr int SEQ_SHUTDOWN = -1;

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
   * @note Config-time: reserves the registry buckets up front.
   */
  explicit SequenceGroup(int maxPhase) : maxPhase_(maxPhase) {
    registry_.reserve(static_cast<std::size_t>(maxPhase > 0 ? maxPhase : 1));
  }

  /**
   * @brief Register a task with this sequence at a specific phase.
   * @param task Task to register.
   * @param phase Phase number (counter value to wait for).
   *
   * Phase numbers should be calculated as:
   *   1 for first task(s), then +1 for each prior task in sequence.
   *
   * @note Config-time: inserts into the registry map (may allocate a
   *       node); keep out of RT paths.
   */
  void addTask(SchedulableTask& task, int phase) { registry_[&task] = SeqInfo{phase, maxPhase_}; }

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
   * @brief Get the phase counter.
   * @return Pointer to the group-owned atomic counter; valid for the
   *         group's lifetime.
   */
  [[nodiscard]] std::atomic<int>* counter() noexcept { return &counter_; }

  /**
   * @brief Get the maximum phase number.
   * @return Max phase (= total task count).
   */
  [[nodiscard]] int maxPhase() const noexcept { return maxPhase_; }

  /**
   * @brief Reset the counter to initial state (phase 1).
   */
  void reset() noexcept { counter_.store(1, std::memory_order_release); }

private:
  std::atomic<int> counter_{1}; ///< Group-owned phase counter.
  int maxPhase_;
  std::unordered_map<const SchedulableTask*, SeqInfo> registry_;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Wait until counter reaches expected phase.
 * @param counter Shared atomic counter.
 * @param expectedPhase Phase to wait for.
 * @param abort Optional abort flag; when it reads true the wait returns
 *        false instead of continuing. The scheduler passes its stopping
 *        flag so a parked waiter cannot outlive a shutdown -- the waker
 *        must notify the counter after setting the flag (repeatedly, to
 *        cover a waiter that parks between pulses).
 * @return true when the phase arrived; false when aborted.
 *
 * Uses hybrid wait: short spin with exponential backoff, then park.
 * Force-inlined for predictable hot path performance.
 */
[[nodiscard]] inline bool waitForPhase(std::atomic<int>& counter, int expectedPhase,
                                       const std::atomic<bool>* abort = nullptr) noexcept {
  // Fast path: check once before spinning
  if (counter.load(std::memory_order_acquire) >= expectedPhase) {
    return true;
  }

  // Hybrid wait: spin first, then park
  // Higher spin budget (256) works better to avoid parking overhead
  constexpr unsigned SPIN_BUDGET = 256;
  apex::helpers::cpu::ExponentialBackoff bk;

  for (unsigned i = 0; i < SPIN_BUDGET; ++i) {
    if (counter.load(std::memory_order_acquire) >= expectedPhase) {
      return true;
    }
    if (abort != nullptr && abort->load(std::memory_order_acquire)) {
      return false;
    }
    bk.spinOnce();
  }

  // Park until ready (or aborted)
  for (;;) {
    const int cur = counter.load(std::memory_order_acquire);
    if (cur >= expectedPhase) {
      return true;
    }
    if (abort != nullptr && abort->load(std::memory_order_acquire)) {
      return false;
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
