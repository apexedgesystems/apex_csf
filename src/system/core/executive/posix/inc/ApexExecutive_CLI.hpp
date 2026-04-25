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
  WATCHDOG_INTERVAL, ///< Watchdog check interval (ms)
  ENABLE_PROFILING,  ///< Enable profiling
  PROFILE_INTERVAL,  ///< Profile sample interval (ticks)
  VERBOSITY,         ///< Log verbosity level
  RT_MODE,           ///< Real-time mode override
  RT_MAX_LAG,        ///< Max lag tolerance (ticks)
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
    {WATCHDOG_INTERVAL, {"--watchdog-interval", 1, false}},
    {ENABLE_PROFILING, {"--enable-profiling", 0, false}},
    {PROFILE_INTERVAL, {"--profile-interval", 1, false}},
    {VERBOSITY, {"--verbosity", 1, false}},
    {RT_MODE, {"--rt-mode", 1, false}},
    {RT_MAX_LAG, {"--rt-max-lag", 1, false}},
};

} // namespace executive

#endif // APEX_SYSTEM_CORE_EXECUTIVE_CLI_HPP
