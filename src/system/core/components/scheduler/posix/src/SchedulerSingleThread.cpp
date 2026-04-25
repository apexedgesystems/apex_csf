/**
 * @file SchedulerSingleThread.cpp
 * @brief Single-threaded scheduler implementation.
 */

#include "src/system/core/components/scheduler/posix/inc/SchedulerSingleThread.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerStatus.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include <cstdint>

#include <fmt/core.h>

namespace system_core {
namespace scheduler {

uint8_t SchedulerSingleThread::doInit() noexcept {
  initSchedulerLog(logDir_);

  componentLog()->info(label(), "Single-threaded scheduler constructed");
  componentLog()->info(label(), fmt::format("Fundamental frequency: {} Hz", ffreq_));
  componentLog()->info(label(), "Execution mode: Sequential");
  componentLog()->info(label(), "");

  logScheduleLayout("Sequential (single-threaded)");

  return static_cast<uint8_t>(Status::SUCCESS);
}

Status SchedulerSingleThread::executeTasksOnTickSingle(std::uint16_t tick) noexcept {
  const std::size_t SIZE = schedule_.size();
  const std::size_t TICK = static_cast<std::size_t>(tick);
  if (SIZE == 0 || TICK >= SIZE) {
    setStatus(static_cast<uint8_t>(Status::SUCCESS));
    return Status::SUCCESS;
  }

  auto& entryIndices = schedule_[TICK];
  const std::size_t N = entryIndices.size();
  if (N == 0) {
    setStatus(static_cast<uint8_t>(Status::SUCCESS));
    return Status::SUCCESS;
  }

  constexpr uint8_t OK = static_cast<uint8_t>(Status::SUCCESS);
  bool anyError = false;

  auto* lg = componentLog();
  const int LOG_LEVEL = lg ? static_cast<int>(lg->level()) : 0;

  for (std::size_t i = 0; i < N; ++i) {
    const std::size_t ENTRY_IDX = entryIndices[i];
    TaskEntry& entry = entries_[ENTRY_IDX];

    // Frequency decimation check (scheduler-owned)
    if (!entry.shouldRun()) {
      continue;
    }

    // Skip tasks belonging to locked components
    if (entry.fullUid != 0 && componentResolver_) {
      auto* comp = componentResolver_->getComponent(entry.fullUid);
      if (comp != nullptr && comp->isLocked()) {
        continue;
      }
    }

    SchedulableTask* t = entry.task;
    try {
      const uint8_t RC = t->execute();
      if (RC != OK) {
        anyError = true;
        if (lg && static_cast<int>(logs::SystemLog::Level::WARNING) >= LOG_LEVEL) {
          lg->warning(label(), static_cast<uint8_t>(Status::WARN_TASK_NON_SUCCESS_RET),
                      fmt::format("Task '{}' returned {} at tick {}", t->getLabel(), RC, tick));
        }
      }
    } catch (...) {
      anyError = true;
      if (lg && static_cast<int>(logs::SystemLog::Level::ERROR) >= LOG_LEVEL) {
        lg->error(label(), static_cast<uint8_t>(Status::ERROR_TASK_EXECUTION_FAIL),
                  fmt::format("Task '{}' threw exception at tick {}", t->getLabel(), tick));
      }
    }
  }

  const Status OUT = anyError ? Status::ERROR_TASK_EXECUTION_FAIL : Status::SUCCESS;
  setStatus(static_cast<uint8_t>(OUT));
  return OUT;
}

} // namespace scheduler
} // namespace system_core
