#ifndef APEX_SYSTEM_CORE_EXECUTIVE_WATCHDOG_HPP
#define APEX_SYSTEM_CORE_EXECUTIVE_WATCHDOG_HPP
/**
 * @file ApexExecutive_Watchdog.hpp
 * @brief Watchdog monitoring with configurable frequency and health checks.
 */

#include <cstdint>

namespace executive {

/**
 * @brief Watchdog state tracking for health monitoring.
 *
 * Tracks clock and task execution progress to detect freezes and lags.
 */
struct WatchdogState {
  std::uint64_t lastClockCycles{0}; // Last observed clock cycle count
  std::uint64_t warningCount{0};    // Total warnings issued
  std::uint32_t frequencyHz{1};     // Watchdog update frequency (Hz)
};

} // namespace executive

#endif // APEX_SYSTEM_CORE_EXECUTIVE_WATCHDOG_HPP