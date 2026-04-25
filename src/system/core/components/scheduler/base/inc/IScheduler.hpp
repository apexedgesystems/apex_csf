#ifndef APEX_SCHEDULER_BASE_ISCHEDULER_HPP
#define APEX_SCHEDULER_BASE_ISCHEDULER_HPP
/**
 * @file IScheduler.hpp
 * @brief Minimal pure interface for task schedulers.
 *
 * Design:
 *   - Standalone scheduler interface (does not inherit IComponent)
 *   - Zero heavy dependencies (no std::filesystem, std::vector in interface)
 *   - Suitable for both ApexExecutive (SchedulerBase) and McuExecutive (SchedulerLite)
 *
 * RT Constraints:
 *   - tick() is RT-safe (the main execution entry point)
 *   - Query methods are RT-safe
 *   - Configuration methods (addTask) are boot-time only
 *
 * Implementations:
 *   - SchedulerBase (apex/) - Full-featured, heap-using, POSIX filesystem
 *   - SchedulerLite (lite/) - Static task table, no heap in hot path
 */

#include <stdint.h>
#include <stddef.h>

namespace system_core {
namespace scheduler {

/* ----------------------------- IScheduler ----------------------------- */

/**
 * @class IScheduler
 * @brief Pure virtual interface for all scheduler implementations.
 *
 * Defines the minimal contract for task scheduling. The core operation
 * is tick() which executes all tasks scheduled for the current tick.
 *
 * Derived implementations:
 *   - SchedulerBase: Full-featured for Linux/RTOS (ApexExecutive)
 *   - SchedulerLite: Minimal for bare-metal MCUs (McuExecutive)
 */
class IScheduler {
public:
  /** @brief Virtual destructor. */
  virtual ~IScheduler() = default;

  /* ----------------------------- Scheduler Identity ----------------------------- */

  /**
   * @brief Get fundamental frequency (ticks per second).
   * @return Scheduling frequency in Hz.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] virtual uint16_t fundamentalFreq() const noexcept = 0;

  /* ----------------------------- Execution ----------------------------- */

  /**
   * @brief Execute one scheduler tick.
   *
   * Runs all tasks scheduled for the current tick according to their
   * frequency, offset, and priority configuration.
   *
   * @note RT-safe: This is the main execution entry point.
   * @note Called by executive at fundamental frequency rate.
   */
  virtual void tick() noexcept = 0;

  /**
   * @brief Get current tick count.
   * @return Number of ticks since scheduler start.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] virtual uint64_t tickCount() const noexcept = 0;

  /* ----------------------------- Statistics ----------------------------- */

  /**
   * @brief Get number of registered tasks.
   * @return Task count.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] virtual size_t taskCount() const noexcept = 0;

protected:
  IScheduler() = default;
  IScheduler(const IScheduler&) = delete;
  IScheduler& operator=(const IScheduler&) = delete;
  IScheduler(IScheduler&&) = default;
  IScheduler& operator=(IScheduler&&) = default;
};

} // namespace scheduler
} // namespace system_core

#endif // APEX_SCHEDULER_BASE_ISCHEDULER_HPP
