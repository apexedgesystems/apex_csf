#ifndef APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERSINGLETHREAD_HPP
#define APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERSINGLETHREAD_HPP
/**
 * @file SchedulerSingleThread.hpp
 * @brief Single-threaded task scheduler (derives from SchedulerBase).
 */

#include "src/system/core/components/scheduler/apex/inc/SchedulerBase.hpp"
#include "src/system/core/components/scheduler/apex/inc/SchedulerStatus.hpp"

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
   * @brief Execute tasks scheduled on a specific tick (single-threaded, sequential).
   */
  [[nodiscard]] Status executeTasksOnTickSingle(std::uint16_t tick) noexcept;

protected:
  /** @brief Delegate tick execution to single-threaded sequential dispatch. */
  void executeTick(std::uint16_t tick) noexcept override {
    static_cast<void>(executeTasksOnTickSingle(tick));
  }

  /**
   * @brief Initialize the scheduler. Called by base init().
   */
  [[nodiscard]] uint8_t doInit() noexcept override;

private:
  std::filesystem::path logDir_; ///< Log directory, used in init().
};

} // namespace scheduler
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERSINGLETHREAD_HPP