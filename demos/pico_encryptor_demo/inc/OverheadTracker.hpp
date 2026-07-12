#ifndef APEX_PICO_ENCRYPTOR_OVERHEAD_TRACKER_HPP
#define APEX_PICO_ENCRYPTOR_OVERHEAD_TRACKER_HPP
/**
 * @file OverheadTracker.hpp
 * @brief No-op overhead tracker for the pico_encryptor_demo.
 *
 * The Cortex-M0+ does not have a DWT cycle counter (that is a Cortex-M3+
 * feature). This stub provides the same interface as the STM32 version
 * but returns zeros for all measurements. The OVERHEAD command protocol
 * still works -- it just reports zero values.
 *
 * @note RT-safe: All methods are O(1) no-ops.
 */

#include "McuExecutive.hpp"

#include <stdint.h>

namespace encryptor {

/* ----------------------------- OverheadStats ----------------------------- */

/**
 * @struct OverheadStats
 * @brief Per-tick CPU cycle statistics (all zero on M0+).
 */
struct OverheadStats {
  uint32_t lastCycles{0};
  uint32_t minCycles{0};
  uint32_t maxCycles{0};
  uint32_t sampleCount{0};

  void reset() noexcept {
    lastCycles = 0;
    minCycles = 0;
    maxCycles = 0;
    sampleCount = 0;
  }
};

/* ----------------------------- OverheadTracker ----------------------------- */

/**
 * @class OverheadTracker
 * @brief No-op overhead tracker for Cortex-M0+ (no DWT).
 *
 * Provides the same API as the STM32 version for CommandDeck compatibility.
 * All cycle measurements return zero.
 */
class OverheadTracker {
public:
  explicit OverheadTracker(executive::mcu::McuExecutive<>& exec) noexcept : exec_(exec), stats_{} {}

  /// No-op on M0+ (no DWT to enable).
  void enableDwt() noexcept {}

  /// No-op on M0+ (no cycle counter).
  void markTickStart() noexcept {}

  /// No-op on M0+ (no cycle counter).
  void markTickEnd() noexcept {}

  /* ----------------------------- Fast-Forward ----------------------------- */

  void setFastForward(bool enabled) noexcept { exec_.setFastForward(enabled); }

  [[nodiscard]] bool isFastForward() const noexcept { return exec_.isFastForward(); }

  /* ----------------------------- Accessors ----------------------------- */

  [[nodiscard]] const OverheadStats& stats() const noexcept { return stats_; }

  void resetStats() noexcept { stats_.reset(); }

  /// Returns 0 on M0+ (no cycle counter, no budget).
  [[nodiscard]] uint32_t budgetCycles() const noexcept { return 0; }

private:
  executive::mcu::McuExecutive<>& exec_;
  OverheadStats stats_;
};

} // namespace encryptor

#endif // APEX_PICO_ENCRYPTOR_OVERHEAD_TRACKER_HPP
