#ifndef APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERSINGLETHREAD_HPP
#define APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERSINGLETHREAD_HPP
/**
 * @file SchedulerSingleThread.hpp
 * @brief Single-threaded task scheduler (derives from SchedulerBase).
 */

#include "src/system/core/components/scheduler/posix/inc/SchedulerBase.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerStatus.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace system_core {
namespace scheduler {

/**
 * @class SchedulerSingleThread
 * @brief Provides single-threaded task scheduling and execution functionality.
 */
class SchedulerSingleThread : public SchedulerBase {
public:
  /**
   * @brief Construct with fundamental frequency and required log directory.
   */
  explicit SchedulerSingleThread(std::uint16_t ffreq, const std::filesystem::path& logDir) noexcept
      : SchedulerBase(ffreq), logDir_(logDir) {}

  ~SchedulerSingleThread() override = default;

  /**
   * @brief Execute the tasks due on a cycle (single-threaded, sequential).
   * @param cycle Full cycle count since the first dispatch.
   */
  [[nodiscard]] Status executeTasksOnCycleSingle(std::uint64_t cycle) noexcept;

  /**
   * @brief Execute the tasks due on a tick index, for callers that dispatch
   *        every tick in order and keep no cycle count of their own.
   * @param tick Tick index within one second, [0, ffreq).
   */
  [[nodiscard]] Status executeTasksOnTickSingle(std::uint16_t tick) noexcept;

protected:
  /** @brief Delegate cycle execution to single-threaded sequential dispatch. */
  void executeCycle(std::uint64_t cycle) noexcept override {
    static_cast<void>(executeTasksOnCycleSingle(cycle));
  }

  /**
   * @brief Initialize the scheduler. Called by base init().
   */
  [[nodiscard]] uint8_t doInit() noexcept override;

private:
  /**
   * @brief Run every entry on one slot in order.
   *
   * One body, two instantiations: the gated one consults a multi-second
   * task's cadence per visit; the other, used for every slot that holds
   * no such task, is the plain loop with no per-visit test.
   * @return True if any task returned an error or threw.
   */
  template <bool kGated>
  bool dispatchSlot(const std::vector<std::size_t>& entryIndices, std::uint64_t cycle,
                    std::uint16_t tick) noexcept;

  std::filesystem::path logDir_; ///< Log directory, used in init().
};

} // namespace scheduler
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERSINGLETHREAD_HPP