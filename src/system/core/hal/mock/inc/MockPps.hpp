#ifndef APEX_HAL_MOCK_PPS_HPP
#define APEX_HAL_MOCK_PPS_HPP
/**
 * @file MockPps.hpp
 * @brief Software-driven IPps implementation for deterministic testing.
 *
 * MockPps implements the IPps contract entirely in user space: tests inject
 * edges with explicit timestamps via injectEdge() and consume them via
 * readCapture(). No hardware, no kernel device, no clocks — every byte of
 * behavior is under the test harness's control.
 *
 * Typical use:
 *
 * @code
 *   apex::hal::MockPps pps;
 *   ASSERT_EQ(pps.init({}), apex::hal::PpsStatus::OK);
 *   pps.injectEdge(1'000'000'000);  // 1 second mark
 *   pps.injectEdge(2'000'000'000);  // 2 second mark
 *
 *   int64_t ts = 0;
 *   EXPECT_EQ(pps.readCapture(ts), apex::hal::PpsStatus::OK);
 *   EXPECT_EQ(ts, 1'000'000'000);
 *   EXPECT_EQ(pps.readCapture(ts), apex::hal::PpsStatus::OK);
 *   EXPECT_EQ(ts, 2'000'000'000);
 *   EXPECT_EQ(pps.readCapture(ts), apex::hal::PpsStatus::NO_NEW_EDGE);
 * @endcode
 *
 * Thread Safety: not thread-safe. Tests are single-threaded.
 *
 * RT-Safety: all methods are O(1) noexcept. The queue is a fixed-size
 * ring buffer; injectEdge() returns false rather than allocating when
 * full.
 */

#include "src/system/core/hal/base/IPps.hpp"

#include <stddef.h>
#include <stdint.h>

namespace apex {
namespace hal {

/* ----------------------------- MockPps ----------------------------- */

/**
 * @class MockPps
 * @brief Test-only IPps implementation backed by an in-memory edge queue.
 */
class MockPps final : public IPps {
public:
  /// Maximum number of unconsumed edges held in the queue.
  static constexpr size_t MAX_PENDING_EDGES = 16;

  MockPps() noexcept = default;
  ~MockPps() override = default;

  MockPps(const MockPps&) = delete;
  MockPps& operator=(const MockPps&) = delete;

  /* ----------------------------- IPps overrides ----------------------------- */

  [[nodiscard]] PpsStatus init(const PpsConfig& config) noexcept override {
    config_ = config;
    initialized_ = true;
    return PpsStatus::OK;
  }

  void deinit() noexcept override { initialized_ = false; }

  [[nodiscard]] bool isInitialized() const noexcept override { return initialized_; }

  [[nodiscard]] PpsStatus readCapture(int64_t& timestampNs) noexcept override {
    if (!initialized_) {
      return PpsStatus::ERROR_NOT_INIT;
    }
    if (errorBudget_ > 0) {
      --errorBudget_;
      ++stats_.errorCount;
      return PpsStatus::ERROR_DEVICE;
    }
    if (pending_ == 0) {
      return PpsStatus::NO_NEW_EDGE;
    }
    timestampNs = queue_[head_];
    head_ = (head_ + 1) % MAX_PENDING_EDGES;
    --pending_;
    ++stats_.captureCount;
    return PpsStatus::OK;
  }

  [[nodiscard]] uint32_t pulseCount() const noexcept override { return pulseCount_; }

  [[nodiscard]] const PpsStats& stats() const noexcept override { return stats_; }

  void resetStats() noexcept override { stats_.reset(); }

  /* ----------------------------- Test injection API ----------------------------- */

  /**
   * @brief Queue a single edge with the given local timestamp.
   * @param timestampNs Local-clock-domain timestamp at which the edge fires.
   * @return true if accepted, false if the queue is full.
   * @note pulseCount is incremented on every accepted edge, mirroring a
   *       real implementation where every observed pulse increments the
   *       counter regardless of whether it has been consumed yet.
   */
  bool injectEdge(int64_t timestampNs) noexcept {
    if (pending_ >= MAX_PENDING_EDGES) {
      return false;
    }
    queue_[tail_] = timestampNs;
    tail_ = (tail_ + 1) % MAX_PENDING_EDGES;
    ++pending_;
    ++pulseCount_;
    return true;
  }

  /**
   * @brief Queue a regularly-spaced series of edges.
   * @param startNs Timestamp of the first edge.
   * @param intervalNs Spacing between consecutive edges (e.g. 1e9 for 1 Hz).
   * @param count Number of edges to inject.
   * @return Number of edges actually accepted (may be less than count if
   *         the queue fills).
   */
  uint32_t injectEdgeSeries(int64_t startNs, int64_t intervalNs, uint32_t count) noexcept {
    uint32_t accepted = 0;
    for (uint32_t i = 0; i < count; ++i) {
      if (!injectEdge(startNs + static_cast<int64_t>(i) * intervalNs)) {
        break;
      }
      ++accepted;
    }
    return accepted;
  }

  /**
   * @brief Force the next @p count readCapture() calls to return ERROR_DEVICE.
   * @param count Number of failed reads to simulate. Default 1.
   * @note Failed reads do not consume queued edges.
   */
  void injectError(uint32_t count = 1) noexcept { errorBudget_ += count; }

  /**
   * @brief Get the configuration that was passed to init().
   * @return Stored config (default-constructed if init() not called).
   */
  [[nodiscard]] const PpsConfig& config() const noexcept { return config_; }

  /**
   * @brief Number of pending unconsumed edges in the queue.
   */
  [[nodiscard]] size_t pendingEdges() const noexcept { return pending_; }

  /**
   * @brief Number of injected error reads still pending.
   */
  [[nodiscard]] uint32_t errorBudget() const noexcept { return errorBudget_; }

  /**
   * @brief Reset everything: queue, pulse count, stats, error budget,
   *        config. Does not change initialized state.
   */
  void resetAll() noexcept {
    head_ = 0;
    tail_ = 0;
    pending_ = 0;
    pulseCount_ = 0;
    stats_.reset();
    errorBudget_ = 0;
    config_ = PpsConfig{};
  }

private:
  bool initialized_ = false;
  PpsConfig config_{};
  PpsStats stats_{};
  uint32_t pulseCount_ = 0;
  uint32_t errorBudget_ = 0;

  int64_t queue_[MAX_PENDING_EDGES] = {};
  size_t head_ = 0;
  size_t tail_ = 0;
  size_t pending_ = 0;
};

} // namespace hal
} // namespace apex

#endif // APEX_HAL_MOCK_PPS_HPP
