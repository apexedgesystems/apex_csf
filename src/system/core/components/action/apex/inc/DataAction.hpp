#ifndef APEX_SYSTEM_CORE_DATA_ACTION_HPP
#define APEX_SYSTEM_CORE_DATA_ACTION_HPP
/**
 * @file DataAction.hpp
 * @brief Runtime action primitives with timing triggers.
 *
 * DataAction is the fundamental operation primitive for the action engine's
 * observe-and-react pipeline. It combines a target (who to address) with a
 * trigger condition (when to fire) and an operation type.
 *
 * ActionType distinguishes two operation modes:
 *   - COMMAND: Opcode + payload routed to target component via bus.
 *   - ARM_CONTROL: Arm or disarm a watchpoint, group, or sequence.
 *
 * The action engine does not mutate data directly. Data mutation (fault
 * injection, value overrides) is handled by support components that the
 * action engine triggers via COMMAND routing.
 *
 * RT-safe: Pure data structure, no allocation or I/O.
 *
 * Usage:
 * @code
 *   // Send a RESET command to component 0x007A00
 *   DataAction cmd{};
 *   cmd.target.fullUid = 0x007A00;
 *   cmd.trigger = ActionTrigger::ON_EVENT;
 *   cmd.triggerParam = 5;  // eventId
 *   cmd.commandOpcode = 0x01;  // RESET
 *   cmd.commandPayload = {0x00, 0x01};  // payload bytes
 *   cmd.commandPayloadLen = 2;
 *
 *   // Arm watchpoint 3 at cycle 100
 *   DataAction arm{};
 *   arm.actionType = ActionType::ARM_CONTROL;
 *   arm.trigger = ActionTrigger::AT_CYCLE;
 *   arm.triggerParam = 100;
 *   arm.armTarget = ArmControlTarget::WATCHPOINT;
 *   arm.armIndex = 3;
 *   arm.armState = true;
 * @endcode
 */

#include "src/system/core/components/action/apex/inc/ActionEngineConfig.hpp"
#include "src/system/core/infrastructure/system_component/apex/inc/DataTarget.hpp"

#include <array>
#include <cstdint>
#include <cstring>

namespace system_core {
namespace data {

/* ----------------------------- Constants ----------------------------- */

/// Maximum number of pending actions in a queue.
constexpr std::size_t ACTION_QUEUE_SIZE = Config::ACTION_QUEUE_SIZE;

/// Maximum command payload size in bytes.
constexpr std::size_t COMMAND_PAYLOAD_MAX = 16;

/* ----------------------------- ActionType ----------------------------- */

/**
 * @enum ActionType
 * @brief Operation mode for an action.
 */
enum class ActionType : std::uint8_t {
  COMMAND = 0,    ///< Opcode + payload routed to target component.
  ARM_CONTROL = 1 ///< Arm or disarm a watchpoint, group, or sequence by index.
};

/**
 * @brief Human-readable string for ActionType.
 * @param t Type value.
 * @return Static string.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline const char* toString(ActionType t) noexcept {
  switch (t) {
  case ActionType::COMMAND:
    return "COMMAND";
  case ActionType::ARM_CONTROL:
    return "ARM_CONTROL";
  }
  return "UNKNOWN";
}

/**
 * @enum ArmControlTarget
 * @brief What kind of entity an ARM_CONTROL action targets.
 */
enum class ArmControlTarget : std::uint8_t {
  WATCHPOINT = 0, ///< Arm/disarm a watchpoint by table index.
  GROUP = 1,      ///< Arm/disarm a watchpoint group by table index.
  SEQUENCE = 2    ///< Arm/disarm a sequence by table index.
};

/**
 * @brief Human-readable string for ArmControlTarget.
 * @param t Target value.
 * @return Static string.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline const char* toString(ArmControlTarget t) noexcept {
  switch (t) {
  case ArmControlTarget::WATCHPOINT:
    return "WATCHPOINT";
  case ArmControlTarget::GROUP:
    return "GROUP";
  case ArmControlTarget::SEQUENCE:
    return "SEQUENCE";
  }
  return "UNKNOWN";
}

/* ----------------------------- ActionTrigger ----------------------------- */

/**
 * @enum ActionTrigger
 * @brief When an action should fire.
 */
enum class ActionTrigger : std::uint8_t {
  IMMEDIATE = 0,    ///< Execute on next manager cycle.
  AT_CYCLE = 1,     ///< At specific scheduler cycle count.
  AFTER_CYCLES = 2, ///< N cycles from now (relative).
  AT_TIME = 3,      ///< At absolute sim time (triggerParam = milliseconds).
  ON_EVENT = 4      ///< When a watchpoint event fires (triggerParam = eventId).
};

/**
 * @brief Human-readable string for ActionTrigger.
 * @param t Trigger value.
 * @return Static string.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline const char* toString(ActionTrigger t) noexcept {
  switch (t) {
  case ActionTrigger::IMMEDIATE:
    return "IMMEDIATE";
  case ActionTrigger::AT_CYCLE:
    return "AT_CYCLE";
  case ActionTrigger::AFTER_CYCLES:
    return "AFTER_CYCLES";
  case ActionTrigger::AT_TIME:
    return "AT_TIME";
  case ActionTrigger::ON_EVENT:
    return "ON_EVENT";
  }
  return "UNKNOWN";
}

/* ----------------------------- ActionStatus ----------------------------- */

/**
 * @enum ActionStatus
 * @brief Status codes for action operations.
 */
enum class ActionStatus : std::uint8_t {
  SUCCESS = 0,      ///< Operation succeeded.
  ERROR_FULL = 1,   ///< Action queue is full.
  ERROR_PARAM = 2,  ///< Invalid parameter.
  ERROR_BOUNDS = 3, ///< Target byte range out of bounds.
  ERROR_TARGET = 4, ///< Target not found in registry.
  PENDING = 5,      ///< Action queued but not yet triggered.
  ACTIVE = 6,       ///< Action currently applied (duration > 0).
  EXPIRED = 7       ///< Action completed (duration elapsed).
};

/**
 * @brief Human-readable string for ActionStatus.
 * @param s Status value.
 * @return Static string.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline const char* toString(ActionStatus s) noexcept {
  switch (s) {
  case ActionStatus::SUCCESS:
    return "SUCCESS";
  case ActionStatus::ERROR_FULL:
    return "ERROR_FULL";
  case ActionStatus::ERROR_PARAM:
    return "ERROR_PARAM";
  case ActionStatus::ERROR_BOUNDS:
    return "ERROR_BOUNDS";
  case ActionStatus::ERROR_TARGET:
    return "ERROR_TARGET";
  case ActionStatus::PENDING:
    return "PENDING";
  case ActionStatus::ACTIVE:
    return "ACTIVE";
  case ActionStatus::EXPIRED:
    return "EXPIRED";
  }
  return "UNKNOWN";
}

/* ----------------------------- DataAction ----------------------------- */

/**
 * @struct DataAction
 * @brief A runtime operation descriptor for the action engine.
 *
 * Combines target addressing with a trigger condition and operation type.
 * The engine evaluates trigger conditions each cycle and dispatches the
 * action when conditions are met.
 *
 * @note RT-safe: Pure POD, no allocation.
 */
struct DataAction {
  DataTarget target{};                        ///< Who to address (COMMAND target or context).
  ActionType actionType{ActionType::COMMAND}; ///< Operation mode.
  ActionTrigger trigger{};                    ///< When to fire.
  ActionStatus status{};                      ///< Current lifecycle state.
  std::uint32_t triggerParam{0};              ///< Cycle count, time (ms), or event ID.

  // COMMAND fields
  std::uint16_t commandOpcode{0};    ///< Command opcode (component-defined).
  std::uint8_t commandPayloadLen{0}; ///< Payload length in bytes.
  std::array<std::uint8_t, COMMAND_PAYLOAD_MAX> commandPayload{}; ///< Command payload.

  // ARM_CONTROL fields
  ArmControlTarget armTarget{}; ///< What entity type to arm/disarm.
  std::uint8_t armIndex{0};     ///< Table index of entity.
  bool armState{false};         ///< True = arm, false = disarm.
};

/* ----------------------------- Helpers ----------------------------- */

/**
 * @brief Initialize a DataAction as a command to a target component.
 * @param action Action to initialize.
 * @param targetUid Target component fullUid.
 * @param trigger When to fire.
 * @param triggerParam Trigger parameter.
 * @param opcode Command opcode.
 * @param payload Pointer to payload bytes (may be nullptr).
 * @param payloadLen Number of payload bytes.
 * @note RT-safe: O(payloadLen).
 */
inline void initCommandAction(DataAction& action, std::uint32_t targetUid, ActionTrigger trigger,
                              std::uint32_t triggerParam, std::uint16_t opcode,
                              const void* payload = nullptr, std::uint8_t payloadLen = 0) noexcept {
  action.target.fullUid = targetUid;
  action.actionType = ActionType::COMMAND;
  action.trigger = trigger;
  action.status = ActionStatus::PENDING;
  action.triggerParam = triggerParam;
  action.commandOpcode = opcode;
  action.commandPayloadLen = (payloadLen > COMMAND_PAYLOAD_MAX)
                                 ? static_cast<std::uint8_t>(COMMAND_PAYLOAD_MAX)
                                 : payloadLen;
  if (payload != nullptr && action.commandPayloadLen > 0) {
    std::memcpy(action.commandPayload.data(), payload, action.commandPayloadLen);
  }
}

/**
 * @brief Initialize a DataAction as an arm/disarm control.
 * @param action Action to initialize.
 * @param armTarget What entity type (WATCHPOINT, GROUP, SEQUENCE).
 * @param index Table index of the entity.
 * @param arm True to arm, false to disarm.
 * @param trigger When to fire.
 * @param triggerParam Trigger parameter.
 * @note RT-safe: O(1).
 */
inline void initArmControlAction(DataAction& action, ArmControlTarget armTarget, std::uint8_t index,
                                 bool arm, ActionTrigger trigger,
                                 std::uint32_t triggerParam) noexcept {
  action.actionType = ActionType::ARM_CONTROL;
  action.trigger = trigger;
  action.status = ActionStatus::PENDING;
  action.triggerParam = triggerParam;
  action.armTarget = armTarget;
  action.armIndex = index;
  action.armState = arm;
}

/**
 * @brief Check if an action is a command (vs data write).
 * @param a Action to check.
 * @return True if actionType is COMMAND.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool isCommand(const DataAction& a) noexcept {
  return a.actionType == ActionType::COMMAND;
}

/**
 * @brief Check if an action is an arm control.
 * @param a Action to check.
 * @return True if actionType is ARM_CONTROL.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool isArmControl(const DataAction& a) noexcept {
  return a.actionType == ActionType::ARM_CONTROL;
}

/**
 * @brief Check if an action is still pending (not yet triggered).
 * @param a Action to check.
 * @return True if status is PENDING.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool isPending(const DataAction& a) noexcept {
  return a.status == ActionStatus::PENDING;
}

/**
 * @brief Check if an action is currently active (applied with duration).
 * @param a Action to check.
 * @return True if status is ACTIVE.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool isActive(const DataAction& a) noexcept {
  return a.status == ActionStatus::ACTIVE;
}

/**
 * @brief Check if an action has expired (duration elapsed).
 * @param a Action to check.
 * @return True if status is EXPIRED.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool isExpired(const DataAction& a) noexcept {
  return a.status == ActionStatus::EXPIRED;
}

} // namespace data
} // namespace system_core

#endif // APEX_SYSTEM_CORE_DATA_ACTION_HPP
