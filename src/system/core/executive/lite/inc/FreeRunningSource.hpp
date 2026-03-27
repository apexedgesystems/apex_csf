#ifndef APEX_EXECUTIVE_LITE_FREE_RUNNING_SOURCE_HPP
#define APEX_EXECUTIVE_LITE_FREE_RUNNING_SOURCE_HPP
/**
 * @file FreeRunningSource.hpp
 * @brief Free-running tick source for testing and benchmarking.
 *
 * Runs at maximum speed without any waiting. Each call to waitForNextTick()
 * returns immediately after incrementing the tick counter.
 *
 * Use cases:
 *   - Unit testing LiteExecutive on desktop
 *   - Benchmarking scheduler overhead
 *   - Integration tests (run N ticks as fast as possible)
 *
 * @note RT-safe: All methods are O(1) with no allocation.
 */

#include "src/system/core/executive/lite/inc/ITickSource.hpp"

#include <stdint.h>

namespace executive {
namespace lite {

/* ----------------------------- FreeRunningSource ----------------------------- */

/**
 * @class FreeRunningSource
 * @brief Tick source that runs at maximum speed.
 *
 * Does not wait between ticks. Useful for:
 *   - Testing: Run N ticks deterministically
 *   - Benchmarking: Measure pure scheduler overhead
 *   - Simulation: Fast-forward time
 */
class FreeRunningSource final : public ITickSource {
public:
  /**
   * @brief Construct with specified frequency.
   * @param freqHz Nominal tick frequency in Hz (default 100).
   *
   * The frequency is reported by tickFrequency() but does not
   * affect actual timing (ticks run as fast as possible).
   */
  explicit FreeRunningSource(uint32_t freqHz = 100) noexcept
      : frequency_(freqHz), tickCount_(0), running_(false) {}

  ~FreeRunningSource() override = default;

  // Non-copyable, non-movable (stateful)
  FreeRunningSource(const FreeRunningSource&) = delete;
  FreeRunningSource& operator=(const FreeRunningSource&) = delete;
  FreeRunningSource(FreeRunningSource&&) = delete;
  FreeRunningSource& operator=(FreeRunningSource&&) = delete;

  /* ----------------------------- ITickSource ----------------------------- */

  /**
   * @brief Increment tick and return immediately.
   *
   * No waiting - runs at maximum speed.
   *
   * @note RT-safe: O(1), no allocation.
   */
  void waitForNextTick() noexcept override {
    if (running_) {
      ++tickCount_;
    }
  }

  /**
   * @brief Get current tick count.
   * @return Number of ticks since start().
   */
  [[nodiscard]] uint32_t currentTick() const noexcept override { return tickCount_; }

  /**
   * @brief Get nominal tick frequency.
   * @return Frequency passed to constructor.
   */
  [[nodiscard]] uint32_t tickFrequency() const noexcept override { return frequency_; }

  /**
   * @brief Start the tick source.
   *
   * Resets tick count and enables tick generation.
   */
  void start() noexcept override {
    tickCount_ = 0;
    running_ = true;
  }

  /**
   * @brief Stop the tick source.
   *
   * Disables tick generation. Tick count is preserved.
   */
  void stop() noexcept override { running_ = false; }

  /**
   * @brief Check if running.
   * @return true if start() called and stop() not yet called.
   */
  [[nodiscard]] bool isRunning() const noexcept override { return running_; }

  /* ----------------------------- Test Helpers ----------------------------- */

  /**
   * @brief Reset tick count to zero.
   *
   * Useful for test setup between test cases.
   */
  void reset() noexcept { tickCount_ = 0; }

  /**
   * @brief Set tick count to specific value.
   * @param count New tick count.
   *
   * Useful for testing wrap-around behavior.
   */
  void setTickCount(uint32_t count) noexcept { tickCount_ = count; }

private:
  uint32_t frequency_; ///< Nominal frequency (Hz)
  uint32_t tickCount_; ///< Current tick count
  bool running_;       ///< Running state
};

} // namespace lite
} // namespace executive

#endif // APEX_EXECUTIVE_LITE_FREE_RUNNING_SOURCE_HPP
