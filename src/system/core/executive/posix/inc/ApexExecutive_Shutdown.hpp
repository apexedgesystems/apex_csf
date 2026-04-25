#ifndef APEX_SYSTEM_CORE_EXECUTIVE_SHUTDOWN_HPP
#define APEX_SYSTEM_CORE_EXECUTIVE_SHUTDOWN_HPP
/**
 * @file ApexExecutive_Shutdown.hpp
 * @brief Shutdown configuration for executive system.
 */

#include <cstdint>

namespace executive {

struct ShutdownConfig {
  enum Mode : std::uint8_t {
    SIGNAL_ONLY = 0, // Wait for OS signal only (default, backward compat)
    SCHEDULED,       // Shutdown at specific system timestamp
    RELATIVE_TIME,   // Shutdown after N seconds from startup completion
    CLOCK_CYCLE,     // Shutdown after N clock cycles
    COMBINED         // Use signal OR programmed condition (whichever first)
  };

  Mode mode{SIGNAL_ONLY};
  std::int64_t shutdownAtEpochNs{0}; // For SCHEDULED/COMBINED modes
  std::uint32_t relativeSeconds{0};  // For RELATIVE_TIME/COMBINED modes
  std::uint64_t targetClockCycle{0}; // For CLOCK_CYCLE/COMBINED modes
  bool allowEarlySignal{true};       // For COMBINED mode - allow signal before condition
  bool skipCleanup{false};           // Skip filesystem archive/cleanup at shutdown
};

/**
 * @enum ShutdownStage
 * @brief Staged shutdown sequence for orderly cleanup.
 */
enum class ShutdownStage : std::uint8_t {
  STAGE_SIGNAL_RECEIVED = 0,
  STAGE_STOP_CLOCK,
  STAGE_DRAIN_TASKS,
  STAGE_CLEANUP_RESOURCES,
  STAGE_FINAL_STATS,
  STAGE_COMPLETE
};

} // namespace executive

#endif // APEX_SYSTEM_CORE_EXECUTIVE_SHUTDOWN_HPP