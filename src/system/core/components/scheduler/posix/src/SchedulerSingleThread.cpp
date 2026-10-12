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
  if (!entries_.empty()) {
    runTablePreflight(); // Re-analyze: the table may predate the log.
  }

  componentLog()->info(label(), "Single-threaded scheduler constructed");
  componentLog()->info(label(), fmt::format("Fundamental frequency: {} Hz", ffreq_));
  componentLog()->info(label(), "Execution mode: Sequential");
  componentLog()->info(label(), "");

  logScheduleLayout("Sequential (single-threaded)");

  return static_cast<uint8_t>(Status::SUCCESS);
}

Status SchedulerSingleThread::executeTasksOnTickSingle(std::uint16_t tick) noexcept {
  return executeTasksOnCycleSingle(cycleForTick(tick));
}

Status SchedulerSingleThread::executeTasksOnCycleSingle(std::uint64_t cycle) noexcept {
  // The cycle is noted before any early return, so a late addTask aligns
  // to the cycle after this one whether or not it had work.
  const std::uint16_t tick = beginDispatchCycle(cycle);
  const std::size_t SIZE = schedule_.size();
  const std::size_t TICK = static_cast<std::size_t>(tick);
  if (SIZE == 0 || TICK >= SIZE) {
    setStatus(static_cast<uint8_t>(Status::SUCCESS));
    return Status::SUCCESS;
  }

  auto& entryIndices = schedule_[TICK];
  if (entryIndices.empty()) {
    setStatus(static_cast<uint8_t>(Status::SUCCESS));
    return Status::SUCCESS;
  }

  // A slot that holds a multi-second task takes the gated loop; every
  // other slot takes the plain one, which tests nothing per visit.
  const bool ANY_ERROR = (slotHasGated_[TICK] != 0U)
                             ? dispatchSlot<true>(entryIndices, cycle, tick)
                             : dispatchSlot<false>(entryIndices, cycle, tick);

  const Status OUT = ANY_ERROR ? Status::ERROR_TASK_EXECUTION_FAIL : Status::SUCCESS;
  setStatus(static_cast<uint8_t>(OUT));
  return OUT;
}

template <bool kGated>
bool SchedulerSingleThread::dispatchSlot(const std::vector<std::size_t>& entryIndices,
                                         [[maybe_unused]] std::uint64_t cycle,
                                         std::uint16_t tick) noexcept {
  constexpr uint8_t OK = static_cast<uint8_t>(Status::SUCCESS);
  bool anyError = false;

  auto* lg = componentLog();
  const int LOG_LEVEL = lg ? static_cast<int>(lg->level()) : 0;

  const std::size_t N = entryIndices.size();
  for (std::size_t i = 0; i < N; ++i) {
    const std::size_t ENTRY_IDX = entryIndices[i];
    TaskEntry& entry = entries_[ENTRY_IDX];

    // Cadence gate: is this cycle one of the task's runs? The instant is
    // consumed here whether or not the task then dispatches.
    if constexpr (kGated) {
      if (entry.gated) {
        TaskCadence& cadence = cadences_[ENTRY_IDX];
        if (!cadence.due(cycle)) {
          continue;
        }
        cadence.advance();
      }
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
  return anyError;
}

} // namespace scheduler
} // namespace system_core
