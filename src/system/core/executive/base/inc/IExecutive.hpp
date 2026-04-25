#ifndef APEX_EXECUTIVE_BASE_IEXECUTIVE_HPP
#define APEX_EXECUTIVE_BASE_IEXECUTIVE_HPP
/**
 * @file IExecutive.hpp
 * @brief Minimal pure interface for executives.
 *
 * Design:
 *   - Standalone executive interface (does not inherit IComponent)
 *   - Zero heavy dependencies (no std::filesystem, std::vector in interface)
 *   - Suitable for both ApexExecutive and McuExecutive
 *
 * RT Constraints:
 *   - run() contains the main loop - RT characteristics depend on implementation
 *   - shutdown() initiates graceful termination
 *   - Query methods are RT-safe
 *
 * Implementations:
 *   - PosixExecutiveBase/ApexExecutive (apex/) - Full-featured, multi-threaded
 *   - McuExecutive (mcu/) - Single-threaded, bare-metal compatible
 */

#include <stdint.h>

namespace executive {

/* ----------------------------- RunResult ----------------------------- */

/**
 * @enum RunResult
 * @brief Result of executive run() operation.
 *
 * Minimal status for portable run() interface.
 * Detailed status codes remain implementation-specific.
 */
enum class RunResult : uint8_t {
  SUCCESS = 0,        ///< Completed normally.
  ERROR_INIT = 1,     ///< Initialization failed.
  ERROR_RUNTIME = 2,  ///< Runtime error during execution.
  ERROR_SHUTDOWN = 3, ///< Shutdown error.
};

/* ----------------------------- IExecutive ----------------------------- */

/**
 * @class IExecutive
 * @brief Pure virtual interface for all executive implementations.
 *
 * Defines the minimal contract for system executives. The core operation
 * is run() which executes the main control/scheduler loop.
 *
 * Derived implementations:
 *   - PosixExecutiveBase/ApexExecutive: Full-featured for Linux/RTOS
 *   - McuExecutive: Minimal for bare-metal MCUs
 */
class IExecutive {
public:
  /** @brief Virtual destructor. */
  virtual ~IExecutive() = default;

  /* ----------------------------- Execution ----------------------------- */

  /**
   * @brief Main executive loop entry point.
   *
   * Runs the executive's main control/scheduler loop until completion
   * or shutdown is requested.
   *
   * @return Run result indicating success or failure mode.
   * @note NOT RT-safe during startup/shutdown phases.
   * @note Inner loop may be RT-safe depending on implementation.
   */
  [[nodiscard]] virtual RunResult run() noexcept = 0;

  /**
   * @brief Request graceful shutdown.
   *
   * Signals the executive to stop and clean up. May be called from
   * signal handler or another thread.
   *
   * @note Thread-safe: Can be called from any context.
   */
  virtual void shutdown() noexcept = 0;

  /**
   * @brief Check if shutdown has been requested.
   * @return true if shutdown() has been called.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] virtual bool isShutdownRequested() const noexcept = 0;

  /* ----------------------------- Statistics ----------------------------- */

  /**
   * @brief Get number of completed execution cycles.
   * @return Cycle count since run() started.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] virtual uint64_t cycleCount() const noexcept = 0;

protected:
  IExecutive() = default;
  IExecutive(const IExecutive&) = delete;
  IExecutive& operator=(const IExecutive&) = delete;
  IExecutive(IExecutive&&) = default;
  IExecutive& operator=(IExecutive&&) = default;
};

} // namespace executive

#endif // APEX_EXECUTIVE_BASE_IEXECUTIVE_HPP
