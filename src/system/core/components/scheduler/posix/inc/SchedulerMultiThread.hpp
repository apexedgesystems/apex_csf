#ifndef APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERMULTITHREAD_HPP
#define APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERMULTITHREAD_HPP
/**
 * @file SchedulerMultiThread.hpp
 * @brief Multi-threaded scheduler with ThreadPool and TaskCtxPool.
 *
 * Supports multiple thread pools for heterogeneous workloads. Tasks are
 * routed to pools based on their poolId in TaskConfig.
 *
 * Note: Dependency handling is temporarily simplified. Full scheduler-side
 * dependency management will be added in a future update.
 */

#include "src/system/core/components/scheduler/posix/inc/SchedulerBase.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerStatus.hpp"
#include "src/utilities/concurrency/inc/ThreadPool.hpp"
#include "src/system/core/components/scheduler/posix/inc/TaskCtxPool.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace system_core {
namespace scheduler {

/**
 * @struct PoolSpec
 * @brief Specification for a thread pool in the scheduler.
 */
struct PoolSpec {
  std::string name{"default"};            ///< Pool name for logging/debugging.
  std::size_t numThreads{0};              ///< Worker count (0 = hardware_concurrency).
  apex::concurrency::PoolConfig config{}; ///< RT configuration for workers.
};

class SchedulerMultiThread : public SchedulerBase {
public:
  /**
   * @brief Construct multi-threaded scheduler with single default pool.
   * @param ffreq Fundamental frequency for scheduling (ticks per second).
   * @param logDir Directory for scheduler log file.
   * @param poolConfig Optional RT configuration for worker threads.
   */
  explicit SchedulerMultiThread(std::uint16_t ffreq, const std::filesystem::path& logDir,
                                const apex::concurrency::PoolConfig& poolConfig = {}) noexcept;

  /**
   * @brief Construct multi-threaded scheduler with multiple pools.
   * @param ffreq Fundamental frequency for scheduling (ticks per second).
   * @param logDir Directory for scheduler log file.
   * @param pools Pool specifications (empty = single default pool).
   */
  explicit SchedulerMultiThread(std::uint16_t ffreq, const std::filesystem::path& logDir,
                                std::vector<PoolSpec> pools) noexcept;

  ~SchedulerMultiThread() noexcept override;

  [[nodiscard]] virtual Status executeTasksOnTickMulti(std::uint16_t tick) noexcept;

  /**
   * @brief Task completion callback (placeholder for future dependency support).
   */
  inline void onTaskComplete(SchedulableTask* /*task*/, std::uint16_t /*tick*/) noexcept {
    // Dependency handling will be added when scheduler-side dependencies are implemented
  }

  /**
   * @brief Check if any pool has running workers.
   */
  [[nodiscard]] bool threadsRunning() const noexcept;

  /**
   * @brief Get number of pools.
   */
  [[nodiscard]] std::size_t poolCount() const noexcept { return pools_.size(); }

  /**
   * @brief Check if any task has a period deadline violation.
   *
   * For HARD_PERIOD_COMPLETE mode: Returns true if any task that should
   * run this tick is still running from a previous invocation.
   */
  [[nodiscard]] bool hasPeriodViolation() const noexcept { return periodViolationsThisTick_ > 0; }

  /**
   * @brief Check and clear sticky period violation flag.
   *
   * Returns true if any period violation occurred since the last call.
   * Atomically clears the flag after reading.
   */
  [[nodiscard]] bool checkAndClearPeriodViolation() noexcept {
    return periodViolationFlag_.exchange(false, std::memory_order_acq_rel);
  }

  void shutdown() noexcept;

  /**
   * @brief Enable skip-on-busy mode.
   *
   * When enabled, tasks that are still running from a previous dispatch
   * will be skipped instead of re-dispatched. This prevents task pileup.
   *
   * @param enable True to enable skip-on-busy, false to disable.
   */
  void setSkipOnBusy(bool enable) noexcept { skipOnBusy_ = enable; }

  /**
   * @brief Check if skip-on-busy mode is enabled.
   */
  [[nodiscard]] bool isSkipOnBusy() const noexcept { return skipOnBusy_; }

  /**
   * @brief Get total number of skipped invocations across all tasks.
   */
  [[nodiscard]] std::size_t totalSkipCount() const noexcept { return totalSkipCount_; }

  /* ----------------------------- Health Query Overrides ----------------------------- */

  [[nodiscard]] std::size_t totalPeriodViolations() const noexcept override {
    return totalPeriodViolations_;
  }
  [[nodiscard]] std::size_t periodViolationsThisTick() const noexcept override {
    return periodViolationsThisTick_;
  }
  [[nodiscard]] std::size_t totalSkips() const noexcept override { return totalSkipCount_; }
  [[nodiscard]] std::uint8_t numPools() const noexcept override {
    return static_cast<std::uint8_t>(pools_.size());
  }
  [[nodiscard]] bool skipOnBusyEnabled() const noexcept override { return skipOnBusy_; }

protected:
  /** @brief Delegate tick execution to multi-threaded dispatch. */
  void executeTick(std::uint16_t tick) noexcept override {
    static_cast<void>(executeTasksOnTickMulti(tick));
  }

  /**
   * @brief Initialize the scheduler. Called by base init().
   */
  [[nodiscard]] std::uint8_t doInit() noexcept override;

  void enqueueTask(TaskEntry* entry, std::uint16_t tick) noexcept;

  static std::uint8_t taskTrampoline(void* raw) noexcept;

private:
  void initPools(std::vector<PoolSpec> specs) noexcept;

  std::vector<std::unique_ptr<apex::concurrency::ThreadPool>> pools_;
  std::vector<std::unique_ptr<TaskCtxPool>> ctxPools_;
  std::vector<std::string> poolNames_;
  std::filesystem::path logDir_;
  std::size_t periodViolationsThisTick_{0};      ///< Count of period violations detected this tick.
  std::size_t totalPeriodViolations_{0};         ///< Cumulative period violations since startup.
  std::atomic<bool> periodViolationFlag_{false}; ///< Sticky flag for period violations.
  bool skipOnBusy_{false};                       ///< Skip tasks still running (SKIP_ON_BUSY mode).
  std::size_t totalSkipCount_{0};                ///< Total skipped invocations across all tasks.
};

} // namespace scheduler
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERMULTITHREAD_HPP
