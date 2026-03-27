#ifndef APEX_SYSTEM_CORE_BASE_COMMAND_RESULT_HPP
#define APEX_SYSTEM_CORE_BASE_COMMAND_RESULT_HPP
/**
 * @file CommandResult.hpp
 * @brief Common status codes returned by handleCommand() (sent as ACK/NAK over Aproto).
 *
 * Part of the base interface layer - no heavy dependencies.
 *
 * Conventions:
 *  - SUCCESS = 0 (ACK).
 *  - Nonzero = error code (NAK).
 *  - EOE_COMMAND_RESULT marks end of common codes; components extend after it.
 */

#include <cstdint>

namespace system_core {
namespace system_component {

/* ----------------------------- CommandResult ----------------------------- */

/**
 * @enum CommandResult
 * @brief Common handleCommand() result codes usable by any component.
 *
 * Component-specific codes extend from EOE_COMMAND_RESULT to avoid collisions.
 */
enum class CommandResult : std::uint8_t {
  SUCCESS = 0,      ///< Command executed successfully (ACK).
  NOT_IMPLEMENTED,  ///< Opcode not recognized by this component.
  INVALID_PAYLOAD,  ///< Payload missing, too short, or malformed.
  INVALID_ARGUMENT, ///< Payload parsed but value out of range.
  TARGET_NOT_FOUND, ///< Referenced component or data not found.
  LOAD_FAILED,      ///< TPRM or configuration load failed.
  EXEC_FAILED,      ///< Execution / operation failed.

  // Marker -- component-specific codes extend from here.
  EOE_COMMAND_RESULT
};

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_CORE_BASE_COMMAND_RESULT_HPP
