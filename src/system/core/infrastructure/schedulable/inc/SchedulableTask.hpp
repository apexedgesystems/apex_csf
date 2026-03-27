#ifndef APEX_SYSTEM_CORE_SCHEDULABLE_SCHEDULABLETASK_HPP
#define APEX_SYSTEM_CORE_SCHEDULABLE_SCHEDULABLETASK_HPP
/**
 * @file SchedulableTask.hpp
 * @brief Minimal schedulable task (callable + label).
 *
 * Design goals:
 *  - Minimal task object (~24 bytes: vtable + TaskFn + label)
 *  - All scheduling configuration lives in scheduler (TaskConfig/TaskEntry)
 *  - Sequencing handled at scheduler level (SequenceGroup + trampoline)
 *  - Zero overhead execute() - direct delegate call
 *
 * Architecture:
 *  - Task = what to execute (callable + label)
 *  - Scheduler = when/how to execute (frequency, priority, pool, sequencing)
 *
 * RT-Safety:
 *  - execute(): Direct delegate call (~5-10ns overhead)
 *  - No heap allocations in hot paths
 *  - All methods noexcept
 */

#include "src/system/core/infrastructure/schedulable/inc/SchedulableTaskBase.hpp"

#include <cstdint>

namespace system_core {
namespace schedulable {

/* ----------------------------- SchedulableTask ----------------------------- */

/**
 * @class SchedulableTask
 * @brief Concrete schedulable task with minimal footprint.
 *
 * This is a simple wrapper around a callable delegate with a label.
 * All scheduling configuration (frequency, priority, pool, sequencing)
 * is owned by the scheduler layer (TaskEntry).
 *
 * Real-time characteristics:
 *  - execute(): Direct delegate call (~5-10ns overhead)
 *  - No heap allocations
 *  - Suitable for hard real-time systems
 */
class SchedulableTask : public SchedulableTaskBase {
public:
  /**
   * @brief Construct a task.
   * @param task  Callable delegate (fnptr + context).
   * @param label Non-owning label view (caller ensures lifetime).
   */
  SchedulableTask(TaskFn task, std::string_view label) noexcept
      : SchedulableTaskBase(task, label) {}

  /** @brief Virtual destructor. */
  ~SchedulableTask() override = default;

  /**
   * @brief Execute the task callable.
   * @return Task-specific status code (0 = success by convention).
   *
   * Direct delegate call with no additional overhead.
   * Sequencing (if needed) is handled by the scheduler trampoline.
   *
   * RT-safe: No allocations, direct function call.
   */
  [[nodiscard]] std::uint8_t execute() noexcept override { return task_(); }

  /**
   * @brief Direct access to callable (for scheduler optimization).
   * @return Reference to internal delegate.
   */
  [[nodiscard]] const TaskFn& callable() const noexcept { return task_; }
};

} // namespace schedulable
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SCHEDULABLE_SCHEDULABLETASK_HPP
