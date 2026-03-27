#ifndef APEX_HIL_DEMO_COMPARATOR_HPP
#define APEX_HIL_DEMO_COMPARATOR_HPP
/**
 * @file HilComparator.hpp
 * @brief Compares ControlCmd streams from real and emulated flight controllers.
 *
 * The comparator reads the last ControlCmd from both HilDriver instances
 * and computes divergence metrics (thrust magnitude difference, per-axis
 * delta). Logs warnings when divergence exceeds configurable thresholds.
 *
 * Scheduled task:
 *   - compare (10 Hz): Read both drivers' last commands, compute diff, log.
 *
 * @note RT-safe in compare() task. NOT RT-safe in doInit().
 */

#include "apps/apex_hil_demo/common/inc/VehicleState.hpp"
#include "apps/apex_hil_demo/driver/inc/HilDriver.hpp"
#include "apps/apex_hil_demo/support/inc/HilComparatorData.hpp"

#include "src/system/core/infrastructure/data/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/apex/inc/SupportComponentBase.hpp"

#include <fmt/format.h>

#include <cmath>
#include <cstdint>

namespace appsim {
namespace support {

using system_core::data::State;
using system_core::data::TunableParam;
using system_core::system_component::Status;
using system_core::system_component::SupportComponentBase;

/* ----------------------------- HilComparator ----------------------------- */

/**
 * @class HilComparator
 * @brief Compares real and emulated flight controller outputs.
 *
 * componentId = 123 (0x7B)
 * fullUid = 0x7B00 (single instance)
 *
 * Requires pointers to both HilDriver instances, set via setDrivers().
 */
class HilComparator final : public SupportComponentBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  static constexpr std::uint16_t COMPONENT_ID = 123;
  static constexpr const char* COMPONENT_NAME = "HilComparator";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }

  /* ----------------------------- Task UIDs ----------------------------- */

  enum class TaskUid : std::uint8_t {
    COMPARE = 1 ///< 10 Hz: compare ControlCmd from both drivers.
  };

  /* ----------------------------- Construction ----------------------------- */

  HilComparator() noexcept = default;
  ~HilComparator() override = default;

  /* ----------------------------- Lifecycle ----------------------------- */

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;

    registerTask<HilComparator, &HilComparator::compare>(
        static_cast<std::uint8_t>(TaskUid::COMPARE), this, "compare");

    registerData(DataCategory::TUNABLE_PARAM, "tunableParams", &tunableParams_.get(),
                 sizeof(ComparatorTunableParams));
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(ComparatorState));

    return static_cast<std::uint8_t>(Status::SUCCESS);
  }

public:
  /* ----------------------------- Task Methods ----------------------------- */

  /**
   * @brief Compare ControlCmd from both drivers (10 Hz).
   *
   * Computes per-axis thrust difference and magnitude delta.
   * Logs a warning if magnitude divergence exceeds threshold.
   *
   * @return 0 on success.
   * @note RT-safe: Bounded floating-point arithmetic + log call.
   */
  std::uint8_t compare() noexcept {
    if (driverReal_ == nullptr || driverEmulated_ == nullptr) {
      return 0;
    }
    if (!driverReal_->hasCommand() || !driverEmulated_->hasCommand()) {
      return 0;
    }

    const auto& REAL = driverReal_->lastCommand();
    const auto& EMUL = driverEmulated_->lastCommand();

    // Per-axis delta
    const float DX = REAL.thrust.x - EMUL.thrust.x;
    const float DY = REAL.thrust.y - EMUL.thrust.y;
    const float DZ = REAL.thrust.z - EMUL.thrust.z;

    // Magnitude difference
    const float MAG_DIFF = std::sqrt(DX * DX + DY * DY + DZ * DZ);

    auto& s = state_.get();
    const auto& p = tunableParams_.get();

    // Track max divergence and running sum
    if (MAG_DIFF > s.maxDivergence) {
      s.maxDivergence = MAG_DIFF;
    }
    lastDivergence_ = MAG_DIFF;
    sumDivergence_ += static_cast<double>(MAG_DIFF);
    lastRealThrustZ_ = REAL.thrust.z;
    lastEmulThrustZ_ = EMUL.thrust.z;
    ++s.compareCount;

    // Threshold breach: count but rate-limit logging (1 per second max)
    if (MAG_DIFF > p.warnThreshold) {
      ++s.warnCount;
    }

    // Log summary every 10 comparisons (~1 Hz at 10 Hz rate)
    // Includes current divergence, mean divergence, and actual thrust values
    auto* log = componentLog();
    if (log != nullptr && (s.compareCount % 10) == 0) {
      const double MEAN_DIV =
          (s.compareCount > 0) ? sumDivergence_ / static_cast<double>(s.compareCount) : 0.0;
      log->info(label(), fmt::format("n={} div={:.4f}N mean={:.4f}N max={:.4f}N warns={} "
                                     "realZ={:.2f} emulZ={:.2f} "
                                     "rTx={} rRx={} eTx={} eRx={}",
                                     s.compareCount, lastDivergence_, MEAN_DIV, s.maxDivergence,
                                     s.warnCount, lastRealThrustZ_, lastEmulThrustZ_,
                                     driverReal_->txCount(), driverReal_->rxCount(),
                                     driverEmulated_->txCount(), driverEmulated_->rxCount()));
    }

    return 0;
  }

  [[nodiscard]] const char* label() const noexcept override { return "HIL_COMP"; }

  /* ----------------------------- Configuration ----------------------------- */

  /**
   * @brief Set pointers to both driver instances.
   * @param real Driver connected to real STM32.
   * @param emulated Driver connected to VirtualFlightCtrl PTY.
   */
  void setDrivers(const driver::HilDriver* real, const driver::HilDriver* emulated) noexcept {
    driverReal_ = real;
    driverEmulated_ = emulated;
  }

  /**
   * @brief Set warning threshold for divergence magnitude.
   * @param threshold Threshold in Newtons.
   */
  void setWarnThreshold(float threshold) noexcept {
    tunableParams_.get().warnThreshold = threshold;
  }

  /* ----------------------------- Accessors ----------------------------- */

  [[nodiscard]] float maxDivergence() const noexcept { return state_.get().maxDivergence; }
  [[nodiscard]] std::uint32_t compareCount() const noexcept { return state_.get().compareCount; }
  [[nodiscard]] std::uint32_t warnCount() const noexcept { return state_.get().warnCount; }
  [[nodiscard]] const ComparatorState& comparatorState() const noexcept { return state_.get(); }
  [[nodiscard]] const ComparatorTunableParams& tunableParams() const noexcept {
    return tunableParams_.get();
  }

private:
  const driver::HilDriver* driverReal_ = nullptr;
  const driver::HilDriver* driverEmulated_ = nullptr;

  TunableParam<ComparatorTunableParams> tunableParams_{};
  State<ComparatorState> state_{};

  // Per-comparison tracking (not in State struct to avoid registry churn)
  float lastDivergence_{0.0F};
  float lastRealThrustZ_{0.0F};
  float lastEmulThrustZ_{0.0F};
  double sumDivergence_{0.0};
};

} // namespace support
} // namespace appsim

#endif // APEX_HIL_DEMO_COMPARATOR_HPP
