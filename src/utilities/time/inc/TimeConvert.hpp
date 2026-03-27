#ifndef APEX_UTILITIES_TIME_CONVERT_HPP
#define APEX_UTILITIES_TIME_CONVERT_HPP
/**
 * @file TimeConvert.hpp
 * @brief Conversion utilities between time standards and units.
 *
 * Provides pure-function conversions between UTC, TAI, GPS, and MET
 * time representations. Also provides unit conversions (seconds to
 * microseconds, cycles to microseconds, etc.) for building ATS
 * step timestamps from human-readable values.
 *
 * All conversions are O(1) and RT-safe.
 *
 * Usage:
 * @code
 *   // Convert 5 seconds at 100Hz to ATS cycle offset
 *   uint64_t cycles = apex::time::secondsToCycles(5.0, 100);  // 500
 *
 *   // Convert UTC microseconds to TAI microseconds
 *   uint64_t tai = apex::time::utcToTai(utcMicros, 37);
 *
 *   // Convert TAI to GPS time
 *   uint64_t gps = apex::time::taiToGps(taiMicros);
 * @endcode
 */

#include "src/utilities/time/inc/TimeBase.hpp"

#include <cstdint>

namespace apex {
namespace time {

/* ----------------------------- Unit Conversions ----------------------------- */

/**
 * @brief Convert seconds (float) to microseconds.
 * @param seconds Time in seconds.
 * @return Microseconds.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline std::uint64_t secondsToMicroseconds(double seconds) noexcept {
  return (seconds >= 0.0) ? static_cast<std::uint64_t>(seconds * 1000000.0) : 0;
}

/**
 * @brief Convert microseconds to seconds (float).
 * @param us Time in microseconds.
 * @return Seconds.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline double microsecondsToSeconds(std::uint64_t us) noexcept {
  return static_cast<double>(us) / 1000000.0;
}

/**
 * @brief Convert seconds to scheduler cycles.
 * @param seconds Time in seconds.
 * @param freqHz Scheduler frequency in Hz.
 * @return Cycle count.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline std::uint32_t secondsToCycles(double seconds, std::uint32_t freqHz) noexcept {
  return (seconds >= 0.0 && freqHz > 0)
             ? static_cast<std::uint32_t>(seconds * static_cast<double>(freqHz))
             : 0;
}

/**
 * @brief Convert scheduler cycles to microseconds.
 * @param cycles Cycle count.
 * @param freqHz Scheduler frequency in Hz.
 * @return Microseconds.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline std::uint64_t cyclesToMicroseconds(std::uint32_t cycles,
                                                        std::uint32_t freqHz) noexcept {
  if (freqHz == 0) {
    return 0;
  }
  return (static_cast<std::uint64_t>(cycles) * 1000000ULL) / freqHz;
}

/**
 * @brief Convert microseconds to scheduler cycles.
 * @param us Microseconds.
 * @param freqHz Scheduler frequency in Hz.
 * @return Cycle count (truncated).
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline std::uint32_t microsecondsToCycles(std::uint64_t us,
                                                        std::uint32_t freqHz) noexcept {
  return static_cast<std::uint32_t>((us * freqHz) / 1000000ULL);
}

/* ----------------------------- Standard Conversions ----------------------------- */

/**
 * @brief Convert UTC microseconds to TAI microseconds.
 * @param utcMicros UTC time in microseconds since Unix epoch.
 * @param taiUtcOffset TAI - UTC offset in seconds (default: 37).
 * @return TAI microseconds.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline std::uint64_t
utcToTai(std::uint64_t utcMicros, std::int32_t taiUtcOffset = TAI_UTC_OFFSET_SECONDS) noexcept {
  return utcMicros + static_cast<std::uint64_t>(taiUtcOffset) * 1000000ULL;
}

/**
 * @brief Convert TAI microseconds to UTC microseconds.
 * @param taiMicros TAI time in microseconds.
 * @param taiUtcOffset TAI - UTC offset in seconds (default: 37).
 * @return UTC microseconds.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline std::uint64_t
taiToUtc(std::uint64_t taiMicros, std::int32_t taiUtcOffset = TAI_UTC_OFFSET_SECONDS) noexcept {
  const std::uint64_t OFFSET = static_cast<std::uint64_t>(taiUtcOffset) * 1000000ULL;
  return (taiMicros >= OFFSET) ? (taiMicros - OFFSET) : 0;
}

/**
 * @brief Convert TAI microseconds to GPS microseconds.
 * @param taiMicros TAI time in microseconds since Unix epoch.
 * @return GPS microseconds since GPS epoch (1980-01-06).
 * @note GPS = TAI - 19 seconds, rebased to GPS epoch.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline std::uint64_t taiToGps(std::uint64_t taiMicros) noexcept {
  const std::uint64_t GPS_OFFSET = static_cast<std::uint64_t>(GPS_TAI_OFFSET_SECONDS) * 1000000ULL;
  const std::uint64_t EPOCH_OFFSET =
      static_cast<std::uint64_t>(GPS_EPOCH_UNIX_SECONDS) * 1000000ULL;
  const std::uint64_t TOTAL_OFFSET = GPS_OFFSET + EPOCH_OFFSET;
  return (taiMicros >= TOTAL_OFFSET) ? (taiMicros - TOTAL_OFFSET) : 0;
}

/**
 * @brief Convert GPS microseconds to TAI microseconds.
 * @param gpsMicros GPS time in microseconds since GPS epoch.
 * @return TAI microseconds since Unix epoch.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline std::uint64_t gpsToTai(std::uint64_t gpsMicros) noexcept {
  return gpsMicros + static_cast<std::uint64_t>(GPS_TAI_OFFSET_SECONDS) * 1000000ULL +
         static_cast<std::uint64_t>(GPS_EPOCH_UNIX_SECONDS) * 1000000ULL;
}

/**
 * @brief Convert UTC microseconds to GPS microseconds.
 * @param utcMicros UTC time in microseconds since Unix epoch.
 * @param taiUtcOffset TAI - UTC offset in seconds (default: 37).
 * @return GPS microseconds since GPS epoch.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline std::uint64_t
utcToGps(std::uint64_t utcMicros, std::int32_t taiUtcOffset = TAI_UTC_OFFSET_SECONDS) noexcept {
  return taiToGps(utcToTai(utcMicros, taiUtcOffset));
}

} // namespace time
} // namespace apex

#endif // APEX_UTILITIES_TIME_CONVERT_HPP
