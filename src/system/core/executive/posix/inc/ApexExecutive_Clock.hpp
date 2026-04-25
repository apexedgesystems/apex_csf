#ifndef APEX_SYSTEM_CORE_EXECUTIVE_CLOCK_HPP
#define APEX_SYSTEM_CORE_EXECUTIVE_CLOCK_HPP
/**
 * @file ApexExecutive_Clock.hpp
 * @brief Clock management with frame overrun detection and RT enforcement.
 */

#include <cstdint>

#include <atomic>

namespace executive {

/**
 * @struct ClockState
 * @brief Runtime state tracking for clock execution.
 *
 * Thread-safe state maintained by clock thread.
 * Read by task execution and watchdog threads.
 */
struct ClockState {
  std::atomic<std::uint64_t> cycles{0};       ///< Total clock cycles executed
  std::atomic<std::int64_t> lastTickNs{0};    ///< Timestamp of last tick (ns since epoch)
  std::atomic<bool> isRunning{false};         ///< Clock synchronization flag
  std::atomic<std::uint64_t> overrunCount{0}; ///< Frame overrun counter
};

/**
 * @enum OverrunCondition
 * @brief Flags indicating frame overrun detection state.
 */
enum OverrunCondition : std::uint8_t {
  NO_OVERRUN = 0,
  STEP_PENDING = 1 << 0, ///< Task execution hasn't consumed previous tick
  POOL_ACTIVE = 1 << 1   ///< Thread pool still executing previous tick's tasks
};

} // namespace executive

#endif // APEX_SYSTEM_CORE_EXECUTIVE_CLOCK_HPP