#ifndef APEX_SYSTEM_CORE_EXECUTIVE_CLI_HPP
#define APEX_SYSTEM_CORE_EXECUTIVE_CLI_HPP
/**
 * @file ApexExecutive_CLI.hpp
 * @brief Command-line argument definitions for executive.
 *
 * CLI arguments allow runtime overrides of TPRM-defined parameters.
 */

#include "src/utilities/helpers/inc/Utilities.hpp"

#include <cstdint>
#include <charconv>
#include <string_view>
#include <system_error>

namespace executive {

/* ----------------------------- CLI Arguments ----------------------------- */

/**
 * @enum Arguments
 * @brief Command-line argument identifiers for executive configuration.
 *
 * These arguments override corresponding TPRM values when provided.
 */
enum Arguments : std::uint8_t {
  CONFIG_FILE = 0,   ///< Path to TPRM config file (required)
  STARTUP_MODE,      ///< Startup mode: auto, interactive, scheduled
  STARTUP_DELAY,     ///< Delay before startup (seconds)
  START_AT,          ///< Scheduled start time (epoch ns)
  SHUTDOWN_MODE,     ///< Shutdown mode: signal, scheduled, relative, cycle, combined
  SHUTDOWN_AT,       ///< Scheduled shutdown time (epoch ns)
  SHUTDOWN_AFTER,    ///< Shutdown after N seconds
  SHUTDOWN_CYCLE,    ///< Shutdown after N clock cycles
  ARCHIVE_PATH,      ///< Custom archive output path
  SKIP_CLEANUP,      ///< Skip filesystem cleanup on shutdown
  TASK_CENSUS,       ///< Run the pre-clock task census before starting
  WATCHDOG_INTERVAL, ///< Watchdog check interval (ms)
  ENABLE_PROFILING,  ///< Enable profiling
  PROFILE_INTERVAL,  ///< Profile sample interval (ticks)
  VERBOSITY,         ///< Log verbosity level
  RT_MODE,           ///< Real-time mode override
  RT_MAX_LAG,        ///< Max lag tolerance (ticks)
  INGEST_POLICY,     ///< TPRM ingest policy (strict|lenient)
  CLOCK_HZ,          ///< Fundamental clock frequency override (Hz)
};

/**
 * @brief Argument map defining CLI flags and their parameters.
 *
 * Format: {enum_value, {flag_string, num_params, is_required}}
 */
static const apex::helpers::args::ArgMap ARG_MAP = {
    {CONFIG_FILE, {"--config", 1, false}},
    {STARTUP_MODE, {"--startup-mode", 1, false}},
    {STARTUP_DELAY, {"--startup-delay", 1, false}},
    {START_AT, {"--start-at", 1, false}},
    {SHUTDOWN_MODE, {"--shutdown-mode", 1, false}},
    {SHUTDOWN_AT, {"--shutdown-at", 1, false}},
    {SHUTDOWN_AFTER, {"--shutdown-after", 1, false}},
    {SHUTDOWN_CYCLE, {"--shutdown-cycle", 1, false}},
    {ARCHIVE_PATH, {"--archive-path", 1, false}},
    {SKIP_CLEANUP, {"--skip-cleanup", 0, false}},
    {TASK_CENSUS, {"--task-census", 0, false}},
    {WATCHDOG_INTERVAL, {"--watchdog-interval", 1, false}},
    {ENABLE_PROFILING, {"--enable-profiling", 0, false}},
    {PROFILE_INTERVAL, {"--profile-interval", 1, false}},
    {VERBOSITY, {"--verbosity", 1, false}},
    {RT_MODE, {"--rt-mode", 1, false}},
    {RT_MAX_LAG, {"--rt-max-lag", 1, false}},
    {INGEST_POLICY, {"--ingest-policy", 1, false}},
    {CLOCK_HZ, {"--clock-hz", 1, false}},
};

namespace cli {

/**
 * @brief Parse a whole decimal token into an unsigned 32-bit value without throwing.
 *
 * The entire token must be digits: no sign, no whitespace, no suffix, no
 * exponent, no fraction. A value that does not fit the type is rejected
 * rather than clamped, so a mistyped flag cannot select a different
 * timing configuration quietly.
 *
 * @param token The argument text.
 * @param[out] out The value, written only on success.
 * @return True when the whole token was a number in range.
 * @note RT-safe: no allocation, no exceptions.
 */
[[nodiscard]] inline bool parseWholeU32(std::string_view token, std::uint32_t& out) noexcept {
  if (token.empty()) {
    return false;
  }
  std::uint32_t value = 0;
  const char* const END = token.data() + token.size();
  const auto RESULT = std::from_chars(token.data(), END, value, 10);
  if (RESULT.ec != std::errc{} || RESULT.ptr != END) {
    return false;
  }
  out = value;
  return true;
}

} // namespace cli

} // namespace executive

#endif // APEX_SYSTEM_CORE_EXECUTIVE_CLI_HPP
