#ifndef APEX_UTILITIES_TIME_BASE_HPP
#define APEX_UTILITIES_TIME_BASE_HPP
/**
 * @file TimeBase.hpp
 * @brief Time standard definitions and abstract time provider interface.
 *
 * Defines the time standards supported by the framework and a lightweight
 * delegate-based time provider that system components (especially the ATS
 * sequencing engine) use to obtain the current time without coupling to
 * a specific clock source.
 *
 * Time providers return microseconds (uint64_t) in their configured standard.
 * The sequencing engine and other consumers are time-standard agnostic --
 * they just compare values from the same provider.
 *
 * Supported standards:
 *   - MONOTONIC: Monotonic uptime (CLOCK_MONOTONIC). Not affected by NTP.
 *   - UTC: Wall clock (CLOCK_REALTIME). Subject to leap seconds and NTP jumps.
 *   - TAI: International Atomic Time. UTC + leap seconds. Monotonic, no jumps.
 *   - GPS: GPS Time. TAI - 19 seconds. Used by GPS receivers.
 *   - MET: Mission Elapsed Time. Monotonic from a configurable epoch.
 *   - CYCLE: Scheduler cycle count (default, no OS dependency).
 *
 * RT-safe: All query functions are O(1) noexcept. Clock reads use POSIX
 * clock_gettime() which is vDSO-accelerated on Linux (no syscall).
 */

#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <cstdint>

namespace apex {
namespace time {

/* ----------------------------- TimeStandard ----------------------------- */

/**
 * @enum TimeStandard
 * @brief Identifies which time reference a provider uses.
 */
enum class TimeStandard : std::uint8_t {
  CYCLE = 0,     ///< Scheduler cycle count (unitless). Default.
  MONOTONIC = 1, ///< System monotonic clock (microseconds since boot).
  UTC = 2,       ///< UTC wall clock (microseconds since Unix epoch).
  TAI = 3,       ///< International Atomic Time (microseconds since TAI epoch).
  GPS = 4,       ///< GPS Time (microseconds since GPS epoch 1980-01-06).
  MET = 5        ///< Mission Elapsed Time (microseconds since mission start).
};

/**
 * @brief Human-readable string for TimeStandard.
 * @param s Standard value.
 * @return Static string.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline const char* toString(TimeStandard s) noexcept {
  switch (s) {
  case TimeStandard::CYCLE:
    return "CYCLE";
  case TimeStandard::MONOTONIC:
    return "MONOTONIC";
  case TimeStandard::UTC:
    return "UTC";
  case TimeStandard::TAI:
    return "TAI";
  case TimeStandard::GPS:
    return "GPS";
  case TimeStandard::MET:
    return "MET";
  }
  return "UNKNOWN";
}

/* ----------------------------- Constants ----------------------------- */

/// TAI - UTC offset as of 2017-01-01 (37 leap seconds). Update when new
/// leap seconds are announced by IERS. Last leap second: 2016-12-31.
constexpr std::int32_t TAI_UTC_OFFSET_SECONDS = 37;

/// GPS epoch offset from Unix epoch: 1980-01-06T00:00:00 UTC.
/// = 315964800 seconds (10 years + 5 days of leap years).
constexpr std::int64_t GPS_EPOCH_UNIX_SECONDS = 315964800;

/// GPS - TAI offset: GPS time = TAI - 19 seconds (fixed since GPS epoch).
constexpr std::int32_t GPS_TAI_OFFSET_SECONDS = 19;

/* ----------------------------- TimeProvider ----------------------------- */

/**
 * @brief Delegate type for time providers.
 *
 * Returns current time as uint64_t microseconds in the provider's
 * configured standard. For CYCLE standard, returns the cycle count directly.
 *
 * Used by the ATS sequencing engine: the engine calls the delegate each
 * tick and compares against step timestamps.
 *
 * @note RT-safe: Implementations must be O(1) noexcept.
 */
using TimeProviderDelegate = apex::concurrency::Delegate<std::uint64_t>;

/* ----------------------------- Timestamp ----------------------------- */

/**
 * @struct Timestamp
 * @brief A time value paired with its standard.
 *
 * Carries both the numeric value and the standard it was measured in,
 * preventing accidental comparison of values from different standards.
 *
 * @note RT-safe: Pure POD.
 */
struct Timestamp {
  std::uint64_t microseconds{0};              ///< Time value in microseconds (or cycles for CYCLE).
  TimeStandard standard{TimeStandard::CYCLE}; ///< Which standard this value is in.
};

/**
 * @brief Compare two timestamps (same standard only).
 * @param a First timestamp.
 * @param b Second timestamp.
 * @return True if same standard and a < b.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool operator<(const Timestamp& a, const Timestamp& b) noexcept {
  return a.standard == b.standard && a.microseconds < b.microseconds;
}

[[nodiscard]] inline bool operator==(const Timestamp& a, const Timestamp& b) noexcept {
  return a.standard == b.standard && a.microseconds == b.microseconds;
}

[[nodiscard]] inline bool operator!=(const Timestamp& a, const Timestamp& b) noexcept {
  return !(a == b);
}

} // namespace time
} // namespace apex

#endif // APEX_UTILITIES_TIME_BASE_HPP
