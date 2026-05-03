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
#include "src/utilities/compatibility/inc/compat_span.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"
#include "src/utilities/time/inc/TimeBase.hpp"

#include <cstdint>
#include <vector>

namespace system_core {
namespace time_server {

/* ----------------------------- TimeServer ----------------------------- */

/**
 * @class TimeServer
 * @brief Core component owning the system's wall-clock correlation.
 */
class TimeServer : public system_component::CoreComponentBase {
public:
  /// Component type identifier. Audited 2026-05-03 against the core range:
  /// 0 Executive, 1 Scheduler, 2 FileSystem, 3 Registry, 4 Interface,
  /// 5 ActionComponent, 6 TimeServer (this). 200+ is the support range.
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

  /// Delegate for reading the host's wall-clock UTC in nanoseconds. Used
  /// only on the FREERUN transition: TimeServer latches CLOCK_REALTIME (or
  /// the platform equivalent) once as the new epoch anchor, so post-PPS
  /// holdover doesn't keep advancing UTC from a long-stale reference.
  /// On bare metal where no wall clock exists, leave this unset and
  /// FREERUN will keep the stale anchor (quality already reflects this).
  using WallClockDelegate = apex::concurrency::Delegate<std::int64_t>;

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

  /**
   * @brief Set the wall-clock delegate used to anchor FREERUN.
   * @note Optional. If unset, FREERUN keeps the previous (stale) anchor.
   */
  void setWallClock(WallClockDelegate delegate) noexcept { wallClock_ = delegate; }

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

  /**
   * @brief Accept a remote primary's TNT in RELAY mode.
   *
   * RELAY mode has no local PPS source; correlation is established by
   * latching the local steady clock at the moment a remote TNT is
   * received. Quality is capped at COARSE because the network link
   * latency between primary and relay (UART, Ethernet SW stack, etc.)
   * is the dominant error term -- typically 0.1-5 ms per the ticket
   * table.
   *
   * Has no effect outside RELAY mode (caller can still dispatch the
   * opcode; TimeServer just ignores it).
   *
   * @note RT-safe.
   */
  void handleAcceptRemoteTnt(const TimeAtNextTone& remote) noexcept;

  /* ----------------------------- Bus command opcodes ----------------------------- */

  /// SET_REFERENCE_TIME -- payload: SetReferenceTime, no response.
  static constexpr std::uint16_t OP_SET_REFERENCE_TIME = 0x0601;
  /// TIME_AT_NEXT_TONE -- outbound broadcast only; never received as a command.
  static constexpr std::uint16_t OP_TIME_AT_NEXT_TONE = 0x0602;
  /// GET_TIME_STATUS -- no payload, response: TimeServerOutput.
  static constexpr std::uint16_t OP_GET_TIME_STATUS = 0x0603;
  /// SET_TIME_MANUAL -- payload: SetTimeManual, no response.
  static constexpr std::uint16_t OP_SET_TIME_MANUAL = 0x0604;
  /// RESET_CORRELATION -- no payload, no response.
  static constexpr std::uint16_t OP_RESET_CORRELATION = 0x0605;
  /// ACCEPT_REMOTE_TNT -- payload: TimeAtNextTone (a remote primary's TNT).
  /// Used by RELAY mode to ingest TNT received over the network. No response.
  static constexpr std::uint16_t OP_ACCEPT_REMOTE_TNT = 0x0606;

  /**
   * @brief Dispatch a bus command to the appropriate handler.
   * @return CommandResult::SUCCESS on ACK, NOT_IMPLEMENTED for unknown
   *         opcodes (delegates to the base class for common opcodes
   *         0x0080-0x00FF), INVALID_PAYLOAD for malformed payloads.
   */
  [[nodiscard]] std::uint8_t handleCommand(std::uint16_t opcode,
                                           apex::compat::rospan<std::uint8_t> payload,
                                           std::vector<std::uint8_t>& response) noexcept override;

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

  /**
   * @brief Compute current UTC nanoseconds using the configured steady-clock
   *        delegate. Returns 0 if no correlation or no steady clock wired.
   * @note RT-safe: O(1) -- one delegate call, one subtract, one add.
   */
  [[nodiscard]] std::int64_t currentUtcNs() const noexcept;

  /**
   * @brief Build a TimeProviderDelegate that returns current UTC microseconds
   *        for use as ActionComponent's ATS time source.
   * @return Delegate wired to read this TimeServer (no copy; pointer to *this).
   * @note RT-safe: O(1). The delegate uses TimeServer's configured steady
   *       clock, so a test that drives a synthetic SteadyClockDelegate sees
   *       deterministic UTC values out of the time provider too. Returns 0
   *       microseconds if correlation is not yet established.
   */
  [[nodiscard]] apex::time::TimeProviderDelegate utcTimeProvider() noexcept;

  /**
   * @brief Default monotonic-clock delegate suitable for setSteadyClock.
   * @return Delegate that calls clock_gettime(CLOCK_MONOTONIC).
   * @note POSIX-only.
   */
  [[nodiscard]] static SteadyClockDelegate defaultSteadyClock() noexcept;

  /**
   * @brief Default wall-clock delegate suitable for setWallClock.
   * @return Delegate that calls clock_gettime(CLOCK_REALTIME) and returns
   *         nanoseconds since the Unix epoch.
   * @note POSIX-only. NTP discipline (when active) keeps this within
   *       ~10 ms of UTC, which is the FREERUN accuracy goal.
   */
  [[nodiscard]] static WallClockDelegate defaultWallClock() noexcept;

protected:
  /// Lifecycle hook called from CoreComponentBase::init(). Registers the
  /// OUTPUT and TUNABLE_PARAM blocks with the registry so consumers can
  /// discover them by category + name without holding a TimeServer
  /// pointer. The correlation state itself is established by the first
  /// successful PPS edge plus reference, not at init().
  [[nodiscard]] std::uint8_t doInit() noexcept override;

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
  WallClockDelegate wallClock_;

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
