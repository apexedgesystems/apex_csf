#ifndef APEX_SYSTEM_CORE_TIME_SERVER_HPP
#define APEX_SYSTEM_CORE_TIME_SERVER_HPP
/**
 * @file TimeServer.hpp
 * @brief Sole time authority for the system.
 *
 * Ingests an external 1PPS edge through an IPps source, optionally pairs
 * it with a UTC reference (from GPS, ground command, or onboard clock),
 * correlates the local steady clock to UTC, and broadcasts the
 * TimeAtNextTone (TNT) message every PPS edge. Components compute UTC
 * by interpolating against the most recent TNT:
 *
 *     utcNow = epochNs + (steady_now_ns - localNs)
 *
 * State machine
 * -------------
 *  valid:   NONE -> VALID -> STALE -> FREERUN -> VALID (recovery)
 *  quality: UNKNOWN -> COARSE -> FINE -> PRECISE
 *
 * Quality progresses as:
 *  - UNKNOWN: boot, no reference yet.
 *  - COARSE:  PPS ticking but no fresh reference (HOLDOVER).
 *  - FINE:    PPS + reference paired, drift estimate not yet stable.
 *  - PRECISE: drift estimate has accumulated >= driftFilterTaps samples.
 *
 * Glitch rejection
 * ----------------
 *  Edges arriving outside the [500ms, 1500ms] window from the previous
 *  edge are counted as glitches and not used to update the correlation.
 *
 * RT-safety
 * ---------
 *  tick() is called from the executive's frame loop; runs in O(1) with
 *  no allocation. processEdge() is bounded by the moving-average filter
 *  size. Broadcast happens via a delegate so the bus integration stays
 *  outside this class.
 */

#include "src/system/core/components/time_server/apex/inc/TimeServerData.hpp"
#include "src/system/core/hal/base/IPps.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/CoreComponentBase.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <cstdint>

namespace system_core {
namespace time_server {

/* ----------------------------- TimeServer ----------------------------- */

/**
 * @class TimeServer
 * @brief Core component owning the system's wall-clock correlation.
 */
class TimeServer : public system_component::CoreComponentBase {
public:
  /// Component type identifier (6 = TimeServer, system component range 1-100).
  static constexpr std::uint16_t COMPONENT_ID = 6;

  /// Component name for collision detection.
  static constexpr const char* COMPONENT_NAME = "TimeServer";

  /// Lower / upper interval bounds for accepting a PPS edge as non-glitch.
  static constexpr std::int64_t MIN_VALID_INTERVAL_NS = 500'000'000LL;
  static constexpr std::int64_t MAX_VALID_INTERVAL_NS = 1'500'000'000LL;

  /// Maximum drift filter taps the moving average will hold (TPRM is clamped
  /// to this).
  static constexpr std::size_t MAX_DRIFT_TAPS = 64;

  /// Delegate the executive injects so TimeServer can read the local steady
  /// clock without coupling to a specific clock source.
  using SteadyClockDelegate = apex::concurrency::Delegate<std::int64_t>;

  /// Delegate the executive injects so TimeServer can broadcast the TNT
  /// without owning the bus.
  using BroadcastTntDelegate = apex::concurrency::Delegate<void, const TimeAtNextTone&>;

  TimeServer() noexcept;
  ~TimeServer() override = default;

  TimeServer(const TimeServer&) = delete;
  TimeServer& operator=(const TimeServer&) = delete;

  /* ----------------------------- Component identity ----------------------------- */

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "TIME"; }

  /* ----------------------------- Wiring (call before init) ----------------------------- */

  /**
   * @brief Set the PPS source. Must be initialized by the caller.
   * @param pps Non-owning pointer; must outlive TimeServer.
   * @note RT-safe: stores pointer.
   */
  void setPpsSource(apex::hal::IPps* pps) noexcept { pps_ = pps; }

  /**
   * @brief Set the steady-clock delegate. Must return monotonic ns in the
   *        same domain as IPps timestamps.
   * @note RT-safe: stores function pointer pair.
   */
  void setSteadyClock(SteadyClockDelegate delegate) noexcept { steadyClock_ = delegate; }

  /**
   * @brief Set the TNT broadcast delegate.
   * @note RT-safe: stores function pointer pair.
   */
  void setBroadcastDelegate(BroadcastTntDelegate delegate) noexcept { broadcastTnt_ = delegate; }

  /// Bring the base-class file-loading overload into scope alongside our
  /// in-memory struct overload below.
  using system_component::SystemComponentBase::loadTprm;

  /**
   * @brief Load TPRM-driven configuration from an in-memory struct.
   * @note RT-safe: small struct copy. Clamps driftFilterTaps to MAX_DRIFT_TAPS.
   */
  void loadTprm(const TimeServerTunableParams& params) noexcept;

  /* ----------------------------- Runtime ----------------------------- */

  /**
   * @brief Run one frame. Polls the PPS source, advances state, broadcasts
   *        TNT on edges and on state transitions.
   * @param currentCycle Current scheduler cycle (used for MET).
   * @note RT-safe: O(driftFilterTaps) per edge, O(1) otherwise.
   */
  void tick(std::uint32_t currentCycle) noexcept;

  /* ----------------------------- Commands ----------------------------- */

  /**
   * @brief Accept a UTC reference time from a provider (GpsDriver, ground,
   *        or sim). Pending until the next PPS edge pairs with it.
   * @note RT-safe.
   */
  void handleSetReferenceTime(const SetReferenceTime& cmd) noexcept;

  /**
   * @brief Set absolute UTC immediately, without a PPS reference.
   *        Quality drops to COARSE; valid becomes VALID.
   * @note RT-safe.
   */
  void handleSetTimeManual(const SetTimeManual& cmd) noexcept;

  /**
   * @brief Tear down all correlation state and return to NONE / UNKNOWN.
   * @note RT-safe.
   */
  void resetCorrelation() noexcept;

  /* ----------------------------- Inspection ----------------------------- */

  /** @brief Most recently published OUTPUT block. */
  [[nodiscard]] const TimeServerOutput& output() const noexcept { return output_; }

  /** @brief Most recently broadcast TNT. */
  [[nodiscard]] const TimeAtNextTone& currentTnt() const noexcept { return tnt_; }

  /** @brief Cumulative count of edges rejected as glitches. */
  [[nodiscard]] std::uint32_t glitchCount() const noexcept { return glitchCount_; }

  /** @brief Number of drift samples currently held by the moving average. */
  [[nodiscard]] std::size_t driftSampleCount() const noexcept { return driftSamplesCount_; }

  /** @brief Compute current UTC by interpolation. Returns 0 if no correlation. */
  [[nodiscard]] std::int64_t computeUtcNs(std::int64_t steadyNowNs) const noexcept;

protected:
  /// Lifecycle hook called from CoreComponentBase::init(). No work to do
  /// here -- TimeServer's correlation state is established by the first
  /// successful PPS edge plus reference, not at init().
  [[nodiscard]] std::uint8_t doInit() noexcept override { return 0; }

private:
  void processEdge(std::int64_t edgeLocalNs, std::uint32_t edgePulseCount) noexcept;
  [[nodiscard]] bool isIntervalValid(std::int64_t intervalNs) const noexcept;
  std::int32_t pushDriftSample(std::int64_t intervalNs) noexcept;
  void updateNextTonePrediction() noexcept;
  void publish() noexcept;
  void checkStaleness(std::int64_t nowNs) noexcept;
  void promoteQualityIfStable() noexcept;
  [[nodiscard]] std::int64_t now() const noexcept { return steadyClock_(); }

  // Wiring
  apex::hal::IPps* pps_ = nullptr;
  SteadyClockDelegate steadyClock_;
  BroadcastTntDelegate broadcastTnt_;

  // Configuration
  TimeServerTunableParams tprm_;

  // Published state
  TimeServerOutput output_{};
  TimeAtNextTone tnt_{};

  // Pending reference waiting to pair with the next edge
  bool refPending_ = false;
  std::int64_t pendingRefEpochNs_ = 0;
  std::uint8_t pendingRefSource_ = static_cast<std::uint8_t>(TimeSource::GPS);
  std::uint8_t pendingRefQuality_ = static_cast<std::uint8_t>(TimeQuality::FINE);
  bool haveReference_ = false;

  // Most recent edge state
  std::int64_t lastEdgeLocalNs_ = 0;
  std::int64_t lastEdgeEpochNs_ = 0;
  std::uint32_t lastEdgePulseCount_ = 0;
  bool haveLastEdge_ = false;

  // Drift estimation: ring buffer of (intervalNs - 1e9) samples, in ppb.
  std::int32_t driftSamples_[MAX_DRIFT_TAPS] = {};
  std::size_t driftSamplesHead_ = 0;
  std::size_t driftSamplesCount_ = 0;
  std::int64_t driftSampleSum_ = 0;
  std::int32_t driftPpb_ = 0;

  // Counters
  std::uint64_t metCycles_ = 0;
  std::uint32_t glitchCount_ = 0;
  std::uint32_t totalPpsCount_ = 0; // cumulative valid edges since boot

  // State
  TimeValid valid_ = TimeValid::NONE;
  TimeQuality quality_ = TimeQuality::UNKNOWN;
  TimeSource source_ = TimeSource::ONBOARD;
  std::uint8_t flags_ = 0;
  bool firstTick_ = true;
};

} // namespace time_server
} // namespace system_core

#endif // APEX_SYSTEM_CORE_TIME_SERVER_HPP
