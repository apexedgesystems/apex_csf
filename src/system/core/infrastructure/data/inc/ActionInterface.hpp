#ifndef APEX_SYSTEM_CORE_DATA_ACTION_INTERFACE_HPP
#define APEX_SYSTEM_CORE_DATA_ACTION_INTERFACE_HPP
/**
 * @file ActionInterface.hpp
 * @brief Runtime engine that orchestrates watchpoints, actions, sequences,
 *        and event notifications each scheduler cycle.
 *
 * ActionInterface is the user-facing orchestrator for all runtime data
 * operations. It holds fixed-size tables of watchpoints, groups, sequences,
 * notifications, and an action queue. Each cycle, processCycle():
 *
 *   1. Evaluates armed watchpoints against live data.
 *   2. Evaluates armed groups against watchpoint results.
 *   3. Dispatches fired eventIds to notifications and sequences.
 *   4. Ticks running sequences, queuing step actions.
 *   5. Processes the action queue (trigger checks, mask application,
 *      command routing, arm control).
 *
 * Connection to registry:
 *   A DataResolveDelegate maps (fullUid, category) -> mutable byte pointer
 *   and block size. The user sets this delegate to connect ActionInterface
 *   to whatever registry or data store backs the system.
 *
 * Connection to components:
 *   A CommandDelegate routes (fullUid, opcode, payload, len) to the
 *   target component. Set by the user for cross-bus command dispatch.
 *
 * RT-safe: All operations bounded, noexcept, no allocation.
 *
 * Usage:
 * @code
 *   ActionInterface engine{};
 *
 *   // Connect to registry
 *   engine.resolver = {myResolverFn, &registryCtx};
 *   engine.commandHandler = {myCmdFn, &busCtx};
 *
 *   // Configure a watchpoint: altitude > 150.0
 *   auto& wp = engine.watchpoints[0];
 *   wp.target = {0x007800, DataCategory::OUTPUT, 36, 4};
 *   wp.predicate = WatchPredicate::GT;
 *   wp.dataType = WatchDataType::FLOAT32;
 *   wp.eventId = 1;
 *   float threshold = 150.0F;
 *   std::memcpy(wp.threshold.data(), &threshold, 4);
 *   wp.armed = true;
 *
 *   // Configure a sequence triggered by eventId=1
 *   auto& seq = engine.sequences[0];
 *   initZeroAction(seq.steps[0].action,
 *                  {0x007A00, DataCategory::OUTPUT, 8, 4},
 *                  ActionTrigger::IMMEDIATE, 0, 50);
 *   seq.steps[0].delayCycles = 10;
 *   seq.stepCount = 1;
 *   seq.eventId = 1;
 *   seq.armed = true;
 *
 *   // Run each scheduler cycle
 *   engine.processCycle(currentCycle);
 * @endcode
 */

#include "src/system/core/infrastructure/data/inc/DataAction.hpp"
#include "src/system/core/infrastructure/data/inc/DataSequence.hpp"
#include "src/system/core/infrastructure/data/inc/DataWatchpoint.hpp"
#include "src/system/core/infrastructure/data/inc/EventNotification.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"
#include "src/utilities/time/inc/TimeBase.hpp"

#include <array>
#include <cstdint>

namespace system_core {
namespace data {

/* ----------------------------- ResolvedData ----------------------------- */

/**
 * @struct ResolvedData
 * @brief Result of resolving a DataTarget to a live byte pointer.
 *
 * Returned by the resolver delegate. If data is nullptr, the target
 * could not be resolved (component not registered, category invalid).
 *
 * @note RT-safe: Pure POD.
 */
struct ResolvedData {
  std::uint8_t* data{nullptr}; ///< Mutable pointer to data bytes.
  std::size_t size{0};         ///< Block size in bytes.
};

/* ----------------------------- Delegates ----------------------------- */

/// Resolves a fullUid + category to a mutable byte pointer and block size.
/// Parameters: (fullUid, category). Returns ResolvedData.
using DataResolveDelegate = apex::concurrency::Delegate<ResolvedData, std::uint32_t, DataCategory>;

/// Routes a command to a target component.
/// Parameters: (fullUid, opcode, payload pointer, payload length).
using CommandDelegate = apex::concurrency::Delegate<void, std::uint32_t, std::uint16_t,
                                                    const std::uint8_t*, std::uint8_t>;

/* ----------------------------- EngineStats ----------------------------- */

/**
 * @struct EngineStats
 * @brief Diagnostic counters for the action engine cycle.
 *
 * Updated each processCycle() call. Useful for monitoring and telemetry.
 *
 * @note RT-safe: Pure POD.
 */
struct EngineStats {
  std::uint32_t totalCycles{0};          ///< Total processCycle() calls.
  std::uint32_t watchpointsFired{0};     ///< Watchpoint edges detected (cumulative).
  std::uint32_t groupsFired{0};          ///< Group edges detected (cumulative).
  std::uint32_t actionsApplied{0};       ///< DATA_WRITE actions applied (cumulative).
  std::uint32_t commandsRouted{0};       ///< COMMAND actions routed (cumulative).
  std::uint32_t armControlsApplied{0};   ///< ARM_CONTROL actions applied (cumulative).
  std::uint32_t sequenceSteps{0};        ///< Sequence step actions fired (cumulative).
  std::uint32_t notificationsInvoked{0}; ///< Notification callbacks invoked (cumulative).
  std::uint32_t resolveFailures{0};      ///< DataTarget resolution failures (cumulative).
  std::uint32_t sequenceTimeouts{0};     ///< Step timeouts fired (cumulative).
  std::uint32_t sequenceRetries{0};      ///< Step retries (cumulative).
  std::uint32_t sequenceAborts{0};       ///< Sequence aborts from timeout (cumulative).
  std::uint32_t rtsLoaded{0};            ///< Standalone RTS files loaded (cumulative).
  std::uint32_t atsLoaded{0};            ///< Standalone ATS files loaded (cumulative).
};

/* ----------------------------- ActionInterface ----------------------------- */

/**
 * @struct ActionInterface
 * @brief Orchestrates watchpoints, actions, sequences, and notifications.
 *
 * Holds all fixed-size tables and drives the evaluate-dispatch-execute
 * loop each scheduler cycle. Connect to the registry via the resolver
 * delegate and to the component bus via the commandHandler delegate.
 *
 * @note RT-safe: No allocation. All tables statically sized.
 */
struct ActionInterface {
  /* ---- Delegates ---- */
  DataResolveDelegate resolver{};                  ///< Maps DataTarget -> byte pointer.
  CommandDelegate commandHandler{};                ///< Routes commands to components.
  apex::time::TimeProviderDelegate timeProvider{}; ///< ATS time source (optional).

  /* ---- Tables ---- */
  std::array<DataWatchpoint, WATCHPOINT_TABLE_SIZE> watchpoints{};
  std::array<WatchpointGroup, WATCHPOINT_GROUP_TABLE_SIZE> groups{};
  std::array<DataSequence, SEQUENCE_TABLE_SIZE> sequences{};
  std::array<EventNotification, EVENT_NOTIFICATION_TABLE_SIZE> notifications{};
  std::array<DataAction, ACTION_QUEUE_SIZE> actions{};
  std::uint8_t actionCount{0}; ///< Number of active entries in action queue.

  /* ---- Diagnostics ---- */
  EngineStats stats{};
};

/* ----------------------------- Action Queue ----------------------------- */

/**
 * @brief Queue an action into the action interface.
 * @param iface ActionInterface to modify.
 * @param action Action to enqueue.
 * @return SUCCESS if queued, ERROR_FULL if queue is at capacity.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline ActionStatus queueAction(ActionInterface& iface,
                                              const DataAction& action) noexcept {
  if (iface.actionCount >= ACTION_QUEUE_SIZE) {
    return ActionStatus::ERROR_FULL;
  }
  iface.actions[iface.actionCount] = action;
  ++iface.actionCount;
  return ActionStatus::SUCCESS;
}

/**
 * @brief Remove an action from the queue by index (swap with last).
 * @param iface ActionInterface to modify.
 * @param index Index of action to remove.
 * @note RT-safe: O(1).
 */
inline void removeAction(ActionInterface& iface, std::uint8_t index) noexcept {
  if (index >= iface.actionCount) {
    return;
  }
  --iface.actionCount;
  if (index < iface.actionCount) {
    iface.actions[index] = iface.actions[iface.actionCount];
  }
  iface.actions[iface.actionCount] = DataAction{};
}

/* ----------------------------- Target Resolution ----------------------------- */

/**
 * @brief Resolve a DataTarget to a mutable byte pointer within a data block.
 * @param iface ActionInterface with resolver delegate.
 * @param target Target to resolve.
 * @param[out] dataLen Effective byte length at the returned pointer.
 * @return Pointer to the target bytes, or nullptr on failure.
 * @note RT-safe: O(1) (delegate call).
 */
[[nodiscard]] inline std::uint8_t* resolveTarget(ActionInterface& iface, const DataTarget& target,
                                                 std::size_t& dataLen) noexcept {
  if (!iface.resolver) {
    return nullptr;
  }

  const ResolvedData BLOCK = iface.resolver(target.fullUid, target.category);
  if (BLOCK.data == nullptr || BLOCK.size == 0) {
    return nullptr;
  }

  if (!isInBounds(target, BLOCK.size)) {
    return nullptr;
  }

  dataLen = effectiveLen(target, BLOCK.size);
  return BLOCK.data + target.byteOffset;
}

/* ----------------------------- Watchpoint Evaluation ----------------------------- */

/**
 * @brief Evaluate all armed watchpoints and collect fired event IDs.
 * @param iface ActionInterface to evaluate.
 * @param[out] firedEvents Array to receive fired event IDs.
 * @param maxEvents Capacity of firedEvents array.
 * @return Number of events fired this cycle.
 * @note RT-safe: O(WATCHPOINT_TABLE_SIZE).
 */
inline std::uint8_t evaluateWatchpoints(ActionInterface& iface, std::uint16_t* firedEvents,
                                        std::size_t maxEvents) noexcept {
  std::uint8_t count = 0;

  for (auto& wp : iface.watchpoints) {
    if (!wp.armed) {
      continue;
    }

    std::size_t dataLen = 0;
    std::uint8_t* data = resolveTarget(iface, wp.target, dataLen);
    if (data == nullptr) {
      ++iface.stats.resolveFailures;
      continue;
    }

    if (evaluateEdge(wp, data, dataLen)) {
      ++iface.stats.watchpointsFired;
      if (count < maxEvents) {
        firedEvents[count] = wp.eventId;
        ++count;
      }
    }
  }

  return count;
}

/* ----------------------------- Group Evaluation ----------------------------- */

/**
 * @brief Evaluate all armed groups and collect fired event IDs.
 * @param iface ActionInterface to evaluate.
 * @param[out] firedEvents Array to append fired event IDs to.
 * @param currentCount Current number of events already in firedEvents.
 * @param maxEvents Capacity of firedEvents array.
 * @return Updated event count (currentCount + new group fires).
 * @note RT-safe: O(WATCHPOINT_GROUP_TABLE_SIZE * WATCHPOINT_GROUP_MAX_REFS).
 */
inline std::uint8_t evaluateGroups(ActionInterface& iface, std::uint16_t* firedEvents,
                                   std::uint8_t currentCount, std::size_t maxEvents) noexcept {
  std::uint8_t count = currentCount;

  for (auto& group : iface.groups) {
    if (!group.armed) {
      continue;
    }

    if (evaluateGroupEdge(group, iface.watchpoints.data(), iface.watchpoints.size())) {
      ++iface.stats.groupsFired;
      if (count < maxEvents) {
        firedEvents[count] = group.eventId;
        ++count;
      }
    }
  }

  return count;
}

/* ----------------------------- Event Dispatch ----------------------------- */

/**
 * @brief Dispatch fired events to notifications and sequences.
 * @param iface ActionInterface to process.
 * @param firedEvents Array of event IDs that fired this cycle.
 * @param eventCount Number of events in the array.
 * @note RT-safe: O(eventCount * (NOTIFICATION_TABLE_SIZE + SEQUENCE_TABLE_SIZE)).
 */
inline void dispatchEvents(ActionInterface& iface, const std::uint16_t* firedEvents,
                           std::uint8_t eventCount) noexcept {
  for (std::uint8_t e = 0; e < eventCount; ++e) {
    const std::uint16_t EVENT_ID = firedEvents[e];

    // Invoke matching notifications
    for (auto& note : iface.notifications) {
      if (shouldNotify(note, EVENT_ID)) {
        // Use fireCount from the source (watchpoint or group)
        invokeNotification(note, EVENT_ID, note.invokeCount);
        ++iface.stats.notificationsInvoked;
      }
    }

    // Start matching sequences
    for (auto& seq : iface.sequences) {
      if (shouldTrigger(seq, EVENT_ID)) {
        startSequence(seq);
      }
    }

    // Activate ON_EVENT actions in the queue
    for (std::uint8_t a = 0; a < iface.actionCount; ++a) {
      auto& action = iface.actions[a];
      if (action.trigger == ActionTrigger::ON_EVENT && action.status == ActionStatus::PENDING &&
          action.triggerParam == EVENT_ID) {
        action.status = ActionStatus::ACTIVE;
        action.cyclesRemaining = action.duration;
      }
    }
  }
}

/* ----------------------------- Sequence Tick ----------------------------- */

/**
 * @brief Copy a step action directly into a queue slot (no intermediate copy).
 * @param iface ActionInterface to modify.
 * @param stepAction Source action from a sequence step.
 * @return True if queued successfully (false if queue full).
 * @note RT-safe: O(1). Single copy instead of copy+queue (saves 116 bytes).
 */
inline bool enqueueStepAction(ActionInterface& iface, const DataAction& stepAction) noexcept {
  if (iface.actionCount >= ACTION_QUEUE_SIZE) {
    return false;
  }
  auto& slot = iface.actions[iface.actionCount];
  slot = stepAction;
  slot.trigger = ActionTrigger::IMMEDIATE;
  slot.status = ActionStatus::ACTIVE;
  slot.cyclesRemaining = slot.duration;
  ++iface.actionCount;
  return true;
}

/**
 * @brief Tick a single sequence for one cycle. Internal helper.
 * @param iface ActionInterface (for resolver, action queue, stats).
 * @param seq Sequence to tick.
 * @param currentCycle Current scheduler cycle (for ATS timing).
 * @return Slot index of another sequence to start (START_RTS), or 0xFF.
 * @note RT-safe: O(1).
 *
 * Handles the full state machine:
 *   - EXECUTING: Queue action and advance (handles branching/START_RTS).
 *   - WAITING: Countdown delay. Check timeout. Transition to
 *     WAITING_CONDITION or EXECUTING when delay done.
 *   - WAITING_CONDITION: Evaluate step condition via resolver.
 *     Transition to EXECUTING when met. Check timeout.
 */
inline std::uint8_t tickSingleSequence(ActionInterface& iface, DataSequence& seq,
                                       std::uint32_t currentCycle) noexcept {
  if (seq.currentStep >= seq.stepCount) {
    seq.status = SequenceStatus::COMPLETE;
    return 0xFF;
  }

  auto& step = seq.steps[seq.currentStep];

  /* ---- WAITING: delay countdown ---- */
  if (seq.status == SequenceStatus::WAITING) {
    // ATS: check absolute offset from start
    if (seq.type == SequenceType::ATS) {
      if (iface.timeProvider) {
        // Time-provider mode: startTime and delayCycles are microseconds
        const std::uint64_t NOW = iface.timeProvider();
        const std::uint64_t TARGET = seq.startTime + step.delayCycles;
        if (NOW >= TARGET) {
          seq.delayRemaining = 0;
        }
      } else {
        // Fallback: cycle-based absolute offset
        const std::uint64_t TARGET_CYCLE = seq.startTime + step.delayCycles;
        if (currentCycle >= TARGET_CYCLE) {
          seq.delayRemaining = 0;
        }
      }
    } else {
      // RTS: decrement relative delay
      if (seq.delayRemaining > 0) {
        --seq.delayRemaining;
      }
    }

    // Check timeout
    if (step.timeoutCycles > 0 && step.timeoutRemaining > 0) {
      --step.timeoutRemaining;
      if (step.timeoutRemaining == 0) {
        ++iface.stats.sequenceTimeouts;
        if (!applyTimeoutPolicy(seq)) {
          ++iface.stats.sequenceAborts;
        } else if (step.retryCount > 0) {
          ++iface.stats.sequenceRetries;
        }
        return 0xFF;
      }
    }

    if (seq.delayRemaining > 0) {
      return 0xFF; // Still counting down
    }

    // Delay done. Transition to wait condition or executing.
    if (step.waitCondition.enabled) {
      seq.status = SequenceStatus::WAITING_CONDITION;
      return 0xFF;
    }
    seq.status = SequenceStatus::EXECUTING;
    // Fall through to EXECUTING dispatch below
  }

  /* ---- WAITING_CONDITION: evaluate step condition ---- */
  if (seq.status == SequenceStatus::WAITING_CONDITION) {
    // Check timeout
    if (step.timeoutCycles > 0 && step.timeoutRemaining > 0) {
      --step.timeoutRemaining;
      if (step.timeoutRemaining == 0) {
        ++iface.stats.sequenceTimeouts;
        if (!applyTimeoutPolicy(seq)) {
          ++iface.stats.sequenceAborts;
        } else if (step.retryCount > 0) {
          ++iface.stats.sequenceRetries;
        }
        return 0xFF;
      }
    }

    // Resolve condition target and evaluate
    std::size_t dataLen = 0;
    std::uint8_t* data = resolveTarget(iface, step.waitCondition.target, dataLen);
    if (data == nullptr || !evaluateStepCondition(step.waitCondition, data, dataLen)) {
      return 0xFF; // Condition not met yet
    }
    seq.status = SequenceStatus::EXECUTING;
    // Fall through to EXECUTING dispatch below
  }

  /* ---- EXECUTING: dispatch action and advance ---- */
  if (seq.status == SequenceStatus::EXECUTING) {
    if (enqueueStepAction(iface, step.action)) {
      ++iface.stats.sequenceSteps;
    }

    std::uint8_t startSlot = 0xFF;
    advanceStep(seq, startSlot);
    return startSlot;
  }

  return 0xFF;
}

/**
 * @brief Tick all running sequences and queue ready step actions.
 * @param iface ActionInterface to process.
 * @param currentCycle Current scheduler cycle (for ATS and stats).
 * @note RT-safe: O(SEQUENCE_TABLE_SIZE).
 *
 * Processes the full state machine for each running sequence.
 * Handles cross-sequence START_RTS triggers after each tick.
 */
inline void tickSequences(ActionInterface& iface, std::uint32_t currentCycle = 0) noexcept {
  for (std::size_t i = 0; i < SEQUENCE_TABLE_SIZE; ++i) {
    auto& seq = iface.sequences[i];
    if (!isRunning(seq)) {
      continue;
    }

    const std::uint8_t START_SLOT = tickSingleSequence(iface, seq, currentCycle);

    // Handle START_RTS: start another sequence
    if (START_SLOT < SEQUENCE_TABLE_SIZE) {
      auto& target = iface.sequences[START_SLOT];
      if (target.armed) {
        const std::uint64_t START_REF = (target.type == SequenceType::ATS && iface.timeProvider)
                                            ? iface.timeProvider()
                                            : static_cast<std::uint64_t>(currentCycle);
        startSequence(target, START_REF);
      }
    }
  }
}

/* ----------------------------- Arm Control ----------------------------- */

/**
 * @brief Apply an ARM_CONTROL action to the interface tables.
 * @param iface ActionInterface to modify.
 * @param action ARM_CONTROL action to apply.
 * @note RT-safe: O(1).
 */
inline void applyArmControl(ActionInterface& iface, const DataAction& action) noexcept {
  switch (action.armTarget) {
  case ArmControlTarget::WATCHPOINT:
    if (action.armIndex < WATCHPOINT_TABLE_SIZE) {
      iface.watchpoints[action.armIndex].armed = action.armState;
    }
    break;
  case ArmControlTarget::GROUP:
    if (action.armIndex < WATCHPOINT_GROUP_TABLE_SIZE) {
      iface.groups[action.armIndex].armed = action.armState;
    }
    break;
  case ArmControlTarget::SEQUENCE:
    if (action.armIndex < SEQUENCE_TABLE_SIZE) {
      iface.sequences[action.armIndex].armed = action.armState;
      if (!action.armState) {
        resetSequence(iface.sequences[action.armIndex]);
      }
    }
    break;
  }
}

/* ----------------------------- Data Write ----------------------------- */

/**
 * @brief Apply a DATA_WRITE action's AND/XOR masks to the target bytes.
 * @param iface ActionInterface with resolver.
 * @param action DATA_WRITE action to apply.
 * @return True if masks were applied successfully.
 * @note RT-safe: O(byteLen).
 */
inline bool applyDataWrite(ActionInterface& iface, const DataAction& action) noexcept {
  std::size_t dataLen = 0;
  std::uint8_t* data = resolveTarget(iface, action.target, dataLen);
  if (data == nullptr) {
    ++iface.stats.resolveFailures;
    return false;
  }

  // Apply mask: byte = (byte & AND) ^ XOR
  for (std::size_t i = 0; i < dataLen && i < FAULT_MAX_MASK_LEN; ++i) {
    data[i] = static_cast<std::uint8_t>((data[i] & action.andMask[i]) ^ action.xorMask[i]);
  }

  return true;
}

/* ----------------------------- Action Processing ----------------------------- */

/**
 * @brief Check if an action's trigger condition is met.
 * @param action Action to check.
 * @param currentCycle Current scheduler cycle count.
 * @return True if the action should activate this cycle.
 * @note RT-safe: O(1).
 *
 * ON_EVENT actions are activated by dispatchEvents(), not here.
 */
[[nodiscard]] inline bool shouldActivate(const DataAction& action,
                                         std::uint32_t currentCycle) noexcept {
  if (action.status != ActionStatus::PENDING) {
    return false;
  }

  switch (action.trigger) {
  case ActionTrigger::IMMEDIATE:
    return true;
  case ActionTrigger::AT_CYCLE:
    return currentCycle >= action.triggerParam;
  case ActionTrigger::AFTER_CYCLES:
    // triggerParam was set relative; check if elapsed
    return currentCycle >= action.triggerParam;
  case ActionTrigger::AT_TIME:
    // Time-based triggers handled externally (sim time != cycle count)
    return false;
  case ActionTrigger::ON_EVENT:
    // Activated by dispatchEvents(), not here
    return false;
  }
  return false;
}

/**
 * @brief Process all actions in the queue for one cycle.
 * @param iface ActionInterface to process.
 * @param currentCycle Current scheduler cycle count.
 * @note RT-safe: O(ACTION_QUEUE_SIZE).
 *
 * For each action:
 *   - PENDING: Check trigger, activate if met.
 *   - ACTIVE: Apply the action, decrement duration, expire if done.
 *   - EXPIRED/SUCCESS: Remove from queue.
 */
inline void processActions(ActionInterface& iface, std::uint32_t currentCycle) noexcept {
  std::uint8_t i = 0;
  while (i < iface.actionCount) {
    auto& action = iface.actions[i];

    // Check trigger for pending actions
    if (action.status == ActionStatus::PENDING) {
      if (shouldActivate(action, currentCycle)) {
        action.status = ActionStatus::ACTIVE;
        action.cyclesRemaining = action.duration;
      } else {
        ++i;
        continue;
      }
    }

    // Process active actions
    if (action.status == ActionStatus::ACTIVE) {
      switch (action.actionType) {
      case ActionType::DATA_WRITE:
        if (applyDataWrite(iface, action)) {
          ++iface.stats.actionsApplied;
        }
        break;

      case ActionType::COMMAND:
        if (iface.commandHandler) {
          iface.commandHandler(action.target.fullUid, action.commandOpcode,
                               action.commandPayload.data(), action.commandPayloadLen);
          ++iface.stats.commandsRouted;
        }
        // Commands are one-shot
        action.status = ActionStatus::EXPIRED;
        break;

      case ActionType::ARM_CONTROL:
        applyArmControl(iface, action);
        ++iface.stats.armControlsApplied;
        action.status = ActionStatus::EXPIRED;
        break;
      }

      // Handle duration for DATA_WRITE
      if (action.actionType == ActionType::DATA_WRITE) {
        if (action.duration == 0) {
          // One-shot: expire immediately after applying
          action.status = ActionStatus::EXPIRED;
        } else if (action.cyclesRemaining > 0) {
          --action.cyclesRemaining;
          if (action.cyclesRemaining == 0) {
            action.status = ActionStatus::EXPIRED;
          }
        }
      }
    }

    // Remove expired actions
    if (action.status == ActionStatus::EXPIRED || action.status == ActionStatus::SUCCESS) {
      removeAction(iface, i);
      // Don't increment i: swap-remove put a new action at this index
    } else {
      ++i;
    }
  }
}

/* ----------------------------- Main Cycle ----------------------------- */

/**
 * @brief Run one full cycle of the action engine.
 * @param iface ActionInterface to process.
 * @param currentCycle Current scheduler cycle count.
 * @note RT-safe: O(tables * queue). Bounded by static table sizes.
 *
 * Call once per scheduler frame. Executes the full pipeline:
 *   1. Evaluate watchpoints -> collect fired eventIds
 *   2. Evaluate groups -> collect fired eventIds
 *   3. Dispatch events -> notifications + sequences + ON_EVENT actions
 *   4. Tick sequences -> queue step actions
 *   5. Process action queue -> apply/route/arm/expire
 */
inline void processCycle(ActionInterface& iface, std::uint32_t currentCycle) noexcept {
  ++iface.stats.totalCycles;

  // Maximum possible events per cycle: all watchpoints + all groups
  constexpr std::size_t MAX_EVENTS = WATCHPOINT_TABLE_SIZE + WATCHPOINT_GROUP_TABLE_SIZE;
  std::array<std::uint16_t, MAX_EVENTS> firedEvents{};

  // 1. Evaluate watchpoints
  std::uint8_t eventCount = evaluateWatchpoints(iface, firedEvents.data(), MAX_EVENTS);

  // 2. Evaluate groups
  eventCount = evaluateGroups(iface, firedEvents.data(), eventCount, MAX_EVENTS);

  // 3. Dispatch events
  if (eventCount > 0) {
    dispatchEvents(iface, firedEvents.data(), eventCount);
  }

  // 4. Tick sequences
  tickSequences(iface, currentCycle);

  // 5. Process actions
  processActions(iface, currentCycle);
}

/* ----------------------------- Reset ----------------------------- */

/**
 * @brief Reset all tables and counters to default state.
 * @param iface ActionInterface to reset.
 * @note RT-safe: O(total table entries).
 *
 * Preserves the resolver and commandHandler delegates.
 */
inline void resetInterface(ActionInterface& iface) noexcept {
  for (auto& wp : iface.watchpoints) {
    wp = DataWatchpoint{};
  }
  for (auto& g : iface.groups) {
    g = WatchpointGroup{};
  }
  for (auto& s : iface.sequences) {
    s = DataSequence{};
  }
  for (auto& n : iface.notifications) {
    n = EventNotification{};
  }
  for (auto& a : iface.actions) {
    a = DataAction{};
  }
  iface.actionCount = 0;
  iface.stats = EngineStats{};
}

} // namespace data
} // namespace system_core

#endif // APEX_SYSTEM_CORE_DATA_ACTION_INTERFACE_HPP
