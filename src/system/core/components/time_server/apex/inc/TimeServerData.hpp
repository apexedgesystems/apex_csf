#ifndef APEX_SYSTEM_CORE_TIME_SERVER_DATA_HPP
#define APEX_SYSTEM_CORE_TIME_SERVER_DATA_HPP
/**
 * @file TimeServerData.hpp
 * @brief Inter-component data types for the TimeServer component.
 *
 * Contents (POD only, no logic):
 *  - Enums:         TimeSource, TimeQuality, TimeValid, TimeServerMode
 *  - TNT message:   TimeAtNextTone (broadcast at 1 Hz)
 *  - Commands:      SetReferenceTime, SetTimeManual
 *  - TPRM:          TimeServerTunableParams
 *  - OUTPUT block:  TimeServerOutput
 *
 * Components that consume time read fields from TimeAtNextTone (broadcast)
 * or from TimeServerOutput (registry). The state machine and correlation
 * logic live in TimeServer.cpp; this header is the contract.
 *
 * RT-safe: all types are PODs; default-construction is trivial.
 */

#include <cstdint>

namespace system_core {
namespace time_server {

/* ----------------------------- Enums ----------------------------- */

/**
 * @enum TimeSource
 * @brief Origin of the UTC reference currently feeding TimeServer.
 */
enum class TimeSource : std::uint8_t {
  GPS = 0,     ///< From a GPS receiver via SET_REFERENCE_TIME.
  GROUND = 1,  ///< From an operator command issued from the ground.
  ONBOARD = 2, ///< From an onboard real-time clock or oscillator.
  MANUAL = 3,  ///< From a one-shot SET_TIME_MANUAL command.
  SIM = 4,     ///< From simulation-injected time.
};

/**
 * @brief Human-readable string for TimeSource.
 * @note RT-safe: O(1) switch over a small enum.
 */
const char* toString(TimeSource s) noexcept;

/**
 * @enum TimeQuality
 * @brief How accurate the current correlation is, qualitatively.
 *
 * Quality climbs as the drift estimator accumulates samples and the
 * reference holds steady. Components consult this to decide whether
 * the current time is good enough for their purpose.
 */
enum class TimeQuality : std::uint8_t {
  UNKNOWN = 0, ///< Boot or no reference yet.
  COARSE = 1,  ///< PPS ticking but no fresh reference (HOLDOVER).
  FINE = 2,    ///< PPS + reference, drift not yet stable.
  PRECISE = 3, ///< PPS + reference + stable drift estimate.
};

/**
 * @brief Human-readable string for TimeQuality.
 * @note RT-safe: O(1) switch over a small enum.
 */
const char* toString(TimeQuality q) noexcept;

/**
 * @enum TimeValid
 * @brief Health of the correlation between local steady clock and UTC.
 */
enum class TimeValid : std::uint8_t {
  NONE = 0,    ///< No correlation established yet (boot).
  VALID = 1,   ///< Correlation fresh; PPS within expected interval.
  STALE = 2,   ///< Correlation was valid but PPS hasn't ticked recently.
  FREERUN = 3, ///< PPS lost, running on monotonic clock alone.
};

/**
 * @brief Human-readable string for TimeValid.
 * @note RT-safe: O(1) switch over a small enum.
 */
const char* toString(TimeValid v) noexcept;

/**
 * @enum TimeServerMode
 * @brief Operating mode (TPRM-selected). Selects which sync source
 *        TimeServer treats as authoritative.
 */
enum class TimeServerMode : std::uint8_t {
  PRIMARY = 0,   ///< Local PPS + onboard reference. Sole authority.
  SECONDARY = 1, ///< Local PPS + remote TNT reference. Cross-checks PRIMARY.
  PTP_SYNC = 2,  ///< IEEE 1588 PTP-disciplined clock as sync source.
  CAN_SYNC = 3,  ///< CAN hardware-timestamped sync frames.
  RELAY = 4,     ///< No local PPS; relays TNT received over the network.
};

/**
 * @brief Human-readable string for TimeServerMode.
 * @note RT-safe: O(1) switch over a small enum.
 */
const char* toString(TimeServerMode m) noexcept;

/* ----------------------------- Flag bits for TNT.flags ----------------------------- */

/// Bit 0: a leap second is announced for the upcoming month.
constexpr std::uint8_t TNT_FLAG_LEAP_SECOND_PENDING = 0x01;
/// Bit 1: TimeServer just switched reference sources.
constexpr std::uint8_t TNT_FLAG_REF_SWITCHOVER = 0x02;

/* ----------------------------- TimeAtNextTone ----------------------------- */

/**
 * @struct TimeAtNextTone
 * @brief Broadcast 1 Hz from TimeServer to all components.
 *
 * Contains the most recent confirmed PPS edge (epochNs + localNs), the
 * predicted UTC at the NEXT edge (drift-adjusted), and quality / source
 * indicators. Consumers compute current UTC by interpolating:
 *
 *     utcNow = epochNs + (steady_now_ns - localNs)
 *
 * with optional drift correction for sub-millisecond requirements.
 *
 * The 40-byte layout is contractual; keep field order and sizes fixed.
 * New fields go in `_reserved` until they need a TNT version bump.
 */
struct TimeAtNextTone {
  std::int64_t epochNs;         ///< UTC at the most recent confirmed PPS edge.
  std::int64_t localNs;         ///< Local steady-clock value at that edge.
  std::int64_t nextToneEpochNs; ///< Predicted UTC at the next PPS edge (drift-adjusted).
  std::int32_t driftPpb;        ///< Local oscillator drift, parts per billion.
  std::uint32_t ppsCount;       ///< Monotonic pulse counter (gap detection).
  std::uint8_t source;          ///< TimeSource enum.
  std::uint8_t quality;         ///< TimeQuality enum.
  std::uint8_t valid;           ///< TimeValid enum.
  std::uint8_t flags;           ///< TNT_FLAG_* bitfield.
  std::uint32_t _reserved;      ///< Pad to 40 bytes; future fields go here.
};

static_assert(sizeof(TimeAtNextTone) == 40, "TimeAtNextTone wire size must be 40 bytes");

/* ----------------------------- SetReferenceTime ----------------------------- */

/**
 * @struct SetReferenceTime
 * @brief Command from a reference-time provider (GpsDriver, ground, sim)
 *        to TimeServer.
 *
 * The provider sends the UTC value valid at its associated PPS edge.
 * TimeServer pairs this with the next PPS edge it sees and produces the
 * authoritative TNT broadcast.
 */
struct SetReferenceTime {
  std::int64_t epochNs;            ///< UTC at the associated PPS edge.
  std::uint32_t referencePpsCount; ///< Provider's count of the associated edge (gap detection).
  std::uint8_t source;             ///< TimeSource enum identifying the caller.
  std::uint8_t quality;            ///< TimeQuality the provider claims.
  std::uint8_t flags;              ///< Reserved.
  std::uint8_t _reserved;          ///< Pad to 16 bytes.
};

static_assert(sizeof(SetReferenceTime) == 16, "SetReferenceTime wire size must be 16 bytes");

/* ----------------------------- SetTimeManual ----------------------------- */

/**
 * @struct SetTimeManual
 * @brief Command to set absolute UTC without a PPS reference.
 *
 * Used when neither GPS nor any other PPS-bearing reference is available
 * (e.g. ground operator types in a time and accepts COARSE quality).
 */
struct SetTimeManual {
  std::int64_t epochNs; ///< UTC to assume effective immediately.
};

static_assert(sizeof(SetTimeManual) == 8, "SetTimeManual wire size must be 8 bytes");

/* ----------------------------- TimeServerTunableParams ----------------------------- */

/**
 * @struct TimeServerTunableParams
 * @brief TPRM-loaded configuration for TimeServer.
 *
 * Defaults are chosen for a typical GPS PRIMARY deployment: PPS device 0,
 * GPS as the reference, 1.5 s of staleness allowed before STALE,
 * 16-tap moving average for drift, 60 s holdover limit before forcing
 * FREERUN.
 */
struct TimeServerTunableParams {
  std::uint8_t mode = static_cast<std::uint8_t>(TimeServerMode::PRIMARY);
  std::uint8_t ppsDeviceIndex = 0;
  std::uint8_t primaryRefSource = static_cast<std::uint8_t>(TimeSource::GPS);
  std::uint8_t _pad0 = 0;
  std::uint32_t maxStalenessUs = 1'500'000;
  std::uint32_t driftFilterTaps = 16;
  std::uint32_t holdoverLimitS = 60;
};

static_assert(sizeof(TimeServerTunableParams) == 16,
              "TimeServerTunableParams TPRM size must be 16 bytes");

/* ----------------------------- TimeServerOutput ----------------------------- */

/**
 * @struct TimeServerOutput
 * @brief OUTPUT data block published by TimeServer to the registry.
 *
 * Components that prefer the registry over the bus broadcast read this
 * block. Same semantic content as TNT plus a few derived fields useful
 * for diagnostics (correlationOffsetNs, metCycles).
 */
struct TimeServerOutput {
  std::int64_t utcEpochNs;          ///< UTC at last confirmed PPS edge (= TNT.epochNs).
  std::uint64_t metCycles;          ///< Mission Elapsed Time in scheduler cycles.
  std::int64_t lastPpsLocalNs;      ///< Local steady_clock at last PPS edge.
  std::int64_t correlationOffsetNs; ///< UTC - local at last edge (utcEpochNs - lastPpsLocalNs).
  std::int64_t nextToneEpochNs;     ///< Predicted UTC at next PPS edge.
  std::int32_t driftEstimatePpb;    ///< Local oscillator drift, parts per billion.
  std::uint32_t ppsCount;           ///< Total PPS edges received since boot.
  std::uint8_t correlationValid;    ///< TimeValid enum.
  std::uint8_t timeSource;          ///< TimeSource enum (active reference).
  std::uint8_t timeQuality;         ///< TimeQuality enum.
  std::uint8_t flags;               ///< TNT_FLAG_* bitfield.
};

static_assert(sizeof(TimeServerOutput) == 56, "TimeServerOutput wire size must be 56 bytes");

} // namespace time_server
} // namespace system_core

#endif // APEX_SYSTEM_CORE_TIME_SERVER_DATA_HPP
