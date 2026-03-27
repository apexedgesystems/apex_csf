#ifndef APEX_UTILITIES_TIME_SYSTEM_CLOCKS_HPP
#define APEX_UTILITIES_TIME_SYSTEM_CLOCKS_HPP
/**
 * @file SystemClocks.hpp
 * @brief Concrete time provider functions for POSIX systems.
 *
 * Each function returns current time as uint64_t microseconds suitable
 * for use with TimeProviderDelegate. All use clock_gettime() which is
 * vDSO-accelerated on Linux (no syscall overhead, ~20ns).
 *
 * Usage:
 * @code
 *   // Wire monotonic clock into ATS engine
 *   iface.timeProvider = {apex::time::monotonicMicroseconds, nullptr};
 *
 *   // Wire MET clock with configurable epoch
 *   static apex::time::MetContext metCtx{bootTimeMicros};
 *   iface.timeProvider = {apex::time::metMicroseconds, &metCtx};
 * @endcode
 *
 * RT-safe: All functions are O(1) noexcept.
 */

#include "src/utilities/time/inc/TimeBase.hpp"

#include <cstdint>
#include <ctime>

namespace apex {
namespace time {

/* ----------------------------- MONOTONIC ----------------------------- */

/**
 * @brief Get monotonic time in microseconds (CLOCK_MONOTONIC).
 * @param ctx Unused (pass nullptr).
 * @return Microseconds since boot. Not affected by NTP or settimeofday.
 * @note RT-safe: O(1), vDSO on Linux.
 */
inline std::uint64_t monotonicMicroseconds(void* /*ctx*/) noexcept {
  struct timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<std::uint64_t>(ts.tv_sec) * 1000000ULL +
         static_cast<std::uint64_t>(ts.tv_nsec) / 1000ULL;
}

/* ----------------------------- UTC ----------------------------- */

/**
 * @brief Get UTC time in microseconds (CLOCK_REALTIME).
 * @param ctx Unused (pass nullptr).
 * @return Microseconds since Unix epoch (1970-01-01T00:00:00Z).
 * @note NOT monotonic: subject to NTP adjustments and leap seconds.
 * @note RT-safe: O(1), vDSO on Linux.
 */
inline std::uint64_t utcMicroseconds(void* /*ctx*/) noexcept {
  struct timespec ts{};
  clock_gettime(CLOCK_REALTIME, &ts);
  return static_cast<std::uint64_t>(ts.tv_sec) * 1000000ULL +
         static_cast<std::uint64_t>(ts.tv_nsec) / 1000ULL;
}

/* ----------------------------- TAI ----------------------------- */

/**
 * @brief Context for TAI clock: carries the current TAI-UTC offset.
 *
 * The offset must be updated when leap seconds are announced by IERS.
 * Default: 37 seconds (current as of 2017-01-01).
 */
struct TaiContext {
  std::int32_t taiUtcOffset{TAI_UTC_OFFSET_SECONDS}; ///< TAI - UTC in seconds.
};

/**
 * @brief Get TAI time in microseconds.
 * @param ctx Pointer to TaiContext (required).
 * @return Microseconds since Unix epoch + TAI-UTC offset.
 * @note Monotonic (no leap second discontinuities).
 * @note RT-safe: O(1).
 */
inline std::uint64_t taiMicroseconds(void* ctx) noexcept {
  const auto* TAI_CTX = static_cast<const TaiContext*>(ctx);
  const std::int32_t OFFSET = (TAI_CTX != nullptr) ? TAI_CTX->taiUtcOffset : TAI_UTC_OFFSET_SECONDS;

  struct timespec ts{};
  clock_gettime(CLOCK_REALTIME, &ts);
  const std::int64_t TAI_SEC = static_cast<std::int64_t>(ts.tv_sec) + OFFSET;
  return static_cast<std::uint64_t>(TAI_SEC) * 1000000ULL +
         static_cast<std::uint64_t>(ts.tv_nsec) / 1000ULL;
}

/* ----------------------------- GPS ----------------------------- */

/**
 * @brief Context for GPS clock: carries TAI-UTC offset (GPS = TAI - 19s).
 */
struct GpsContext {
  std::int32_t taiUtcOffset{TAI_UTC_OFFSET_SECONDS}; ///< TAI - UTC in seconds.
};

/**
 * @brief Get GPS time in microseconds since GPS epoch (1980-01-06).
 * @param ctx Pointer to GpsContext (required).
 * @return Microseconds since GPS epoch.
 * @note GPS = TAI - 19 seconds, referenced from GPS epoch.
 * @note RT-safe: O(1).
 */
inline std::uint64_t gpsMicroseconds(void* ctx) noexcept {
  const auto* GPS_CTX = static_cast<const GpsContext*>(ctx);
  const std::int32_t TAI_OFFSET =
      (GPS_CTX != nullptr) ? GPS_CTX->taiUtcOffset : TAI_UTC_OFFSET_SECONDS;

  struct timespec ts{};
  clock_gettime(CLOCK_REALTIME, &ts);

  // GPS = UTC + (TAI-UTC) - 19 - GPS_EPOCH
  const std::int64_t GPS_SEC = static_cast<std::int64_t>(ts.tv_sec) + TAI_OFFSET -
                               GPS_TAI_OFFSET_SECONDS - GPS_EPOCH_UNIX_SECONDS;
  if (GPS_SEC < 0) {
    return 0;
  }
  return static_cast<std::uint64_t>(GPS_SEC) * 1000000ULL +
         static_cast<std::uint64_t>(ts.tv_nsec) / 1000ULL;
}

/* ----------------------------- MET ----------------------------- */

/**
 * @brief Context for Mission Elapsed Time: configurable epoch.
 *
 * Set epochMicroseconds to the monotonic time at mission start.
 * MET = monotonicNow - epochMicroseconds.
 */
struct MetContext {
  std::uint64_t epochMicroseconds{0}; ///< Monotonic time at mission start.
};

/**
 * @brief Get Mission Elapsed Time in microseconds.
 * @param ctx Pointer to MetContext (required).
 * @return Microseconds since mission start (epoch).
 * @note Monotonic (based on CLOCK_MONOTONIC).
 * @note RT-safe: O(1).
 */
inline std::uint64_t metMicroseconds(void* ctx) noexcept {
  const auto* MET_CTX = static_cast<const MetContext*>(ctx);
  const std::uint64_t NOW = monotonicMicroseconds(nullptr);
  const std::uint64_t EPOCH = (MET_CTX != nullptr) ? MET_CTX->epochMicroseconds : 0;
  return (NOW >= EPOCH) ? (NOW - EPOCH) : 0;
}

} // namespace time
} // namespace apex

#endif // APEX_UTILITIES_TIME_SYSTEM_CLOCKS_HPP
