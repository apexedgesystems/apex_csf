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

#include "src/system/core/components/action/apex/inc/DataAction.hpp"
#include "src/system/core/components/action/apex/inc/DataSequence.hpp"
#include "src/system/core/components/action/apex/inc/DataWatchpoint.hpp"
#include "src/system/core/components/action/apex/inc/EventNotification.hpp"
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
  std::uint32_t totalCycles{0};           ///< Total processCycle() calls.
  std::uint32_t watchpointsFired{0};      ///< Watchpoint edges detected (cumulative).
  std::uint32_t groupsFired{0};           ///< Group edges detected (cumulative).
  std::uint32_t actionsApplied{0};        ///< Actions applied (cumulative).
  std::uint32_t commandsRouted{0};        ///< COMMAND actions routed (cumulative).
  std::uint32_t armControlsApplied{0};    ///< ARM_CONTROL actions applied (cumulative).
  std::uint32_t sequenceSteps{0};         ///< Sequence step actions fired (cumulative).
  std::uint32_t notificationsInvoked{0};  ///< Notification callbacks invoked (cumulative).
  std::uint32_t resolveFailures{0};       ///< DataTarget resolution failures (cumulative).
  std::uint32_t sequenceTimeouts{0};      ///< Step timeouts fired (cumulative).
  std::uint32_t sequenceRetries{0};       ///< Step retries (cumulative).
  std::uint32_t sequenceAborts{0};        ///< Sequence aborts from timeout (cumulative).
  std::uint32_t rtsLoaded{0};             ///< Standalone RTS files loaded (cumulative).
  std::uint32_t atsLoaded{0};             ///< Standalone ATS files loaded (cumulative).
  std::uint32_t abortEventsDispatched{0}; ///< Abort events dispatched (cumulative).
  std::uint32_t exclusionStops{0};        ///< Sequences stopped by mutual exclusion (cumulative).
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

  /// Called when an event fires and no resident sequence matches.
  /// The handler should do catalog lookup by eventId + on-demand load.
  /// Parameters: (eventId). No return value.
  apex::concurrency::Delegate<void, std::uint16_t> eventSequenceHandler{};

  /// Called when a sequence step chains to another via START_RTS.
  /// The handler should do catalog lookup by sequenceId + on-demand load.
  /// Parameters: (sequenceId). No return value.
  apex::concurrency::Delegate<void, std::uint16_t> chainSequenceHandler{};

  /* ---- Tables ---- */
  std::array<DataWatchpoint, WATCHPOINT_TABLE_SIZE> watchpoints{};
  std::array<WatchpointGroup, WATCHPOINT_GROUP_TABLE_SIZE> groups{};
  std::array<DataSequence, SEQUENCE_TABLE_SIZE> sequences{};
  std::array<EventNotification, EVENT_NOTIFICATION_TABLE_SIZE> notifications{};
  std::array<DataAction, ACTION_QUEUE_SIZE> actions{};
  std::uint8_t actionCount{0}; ///< Number of active entries in action queue.

  /* ---- Diagnostics ---- */
  EngineStats stats{};

  /* ---- Lookup Tables ---- */
  /// Maps watchpointId -> active table index (0xFF = not active).
  /// Enables O(1) group ref resolution instead of O(tableSize) scan.
  std::array<std::uint8_t, Config::WATCHPOINT_ID_LIMIT> wpIdToIndex{};

  /// Maps eventId -> bitmask of notification table indices that listen for it.
  /// Bit N set = notifications[N] is armed for this event.
  std::array<std::uint32_t, Config::EVENT_ID_LIMIT> eventToNotifications{};

  /// Maps eventId -> bitmask of sequence table indices bound to it.
  /// Bit N set = sequences[N] is armed for this event.
  std::array<std::uint16_t, Config::EVENT_ID_LIMIT> eventToSequences{};
};

/* ----------------------------- Watchpoint Index ----------------------------- */

/**
 * @brief Rebuild the watchpointId-to-index lookup table.
 * @param iface ActionInterface to update.
 * @note NOT RT-safe: O(WATCHPOINT_TABLE_SIZE). Call after activate/deactivate.
 *
 * Scans the active watchpoint table and populates wpIdToIndex so that
 * evaluateGroup can resolve refs in O(1) instead of O(tableSize).
 */
inline void rebuildWatchpointIndex(ActionInterface& iface) noexcept {
  iface.wpIdToIndex.fill(0xFF);
  for (std::size_t i = 0; i < WATCHPOINT_TABLE_SIZE; ++i) {
    const auto ID = iface.watchpoints[i].watchpointId;
    if (iface.watchpoints[i].armed && ID > 0 && ID < Config::WATCHPOINT_ID_LIMIT) {
      iface.wpIdToIndex[ID] = static_cast<std::uint8_t>(i);
    }
  }
}

/**
 * @brief Rebuild the eventId-to-notification and eventId-to-sequence bitmasks.
 * @param iface ActionInterface to update.
 * @note NOT RT-safe. Call after notification/sequence table changes.
 */
inline void rebuildEventIndex(ActionInterface& iface) noexcept {
  iface.eventToNotifications.fill(0);
  iface.eventToSequences.fill(0);

  for (std::size_t i = 0; i < EVENT_NOTIFICATION_TABLE_SIZE; ++i) {
    const auto& NOTE = iface.notifications[i];
    if (NOTE.armed && NOTE.eventId > 0 && NOTE.eventId < Config::EVENT_ID_LIMIT &&
        (NOTE.callback || NOTE.hasLogMessage())) {
      iface.eventToNotifications[NOTE.eventId] |= (1u << i);
    }
  }

  for (std::size_t i = 0; i < SEQUENCE_TABLE_SIZE; ++i) {
    const auto& SEQ = iface.sequences[i];
    if (SEQ.armed && SEQ.eventId > 0 && SEQ.eventId < Config::EVENT_ID_LIMIT) {
      iface.eventToSequences[SEQ.eventId] |= static_cast<std::uint16_t>(1u << i);
    }
  }
}

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
                                        std::size_t maxEvents,
                                        std::uint32_t currentCycle = 0) noexcept {
  std::uint8_t count = 0;

  for (auto& wp : iface.watchpoints) {
    if (!wp.armed) {
      continue;
    }

    // Cadence check: skip if not due this tick.
    // cadenceTicks == 0 means evaluate every tick (default).
    if (wp.cadenceTicks > 0 && (currentCycle % wp.cadenceTicks) != 0) {
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

    if (evaluateGroupEdge(group, iface.watchpoints.data(), iface.watchpoints.size(),
                          iface.wpIdToIndex.data())) {
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
 * @note RT-safe: O(eventCount * popcount(bitmask)) with index,
 *       O(eventCount * tableSize) fallback for out-of-range IDs.
 */
inline void dispatchEvents(ActionInterface& iface, const std::uint16_t* firedEvents,
                           std::uint8_t eventCount) noexcept {
  for (std::uint8_t e = 0; e < eventCount; ++e) {
    const std::uint16_t EVENT_ID = firedEvents[e];

    // Invoke matching notifications
    if (EVENT_ID > 0 && EVENT_ID < Config::EVENT_ID_LIMIT) {
      // Fast path: bitmask lookup
      std::uint32_t mask = iface.eventToNotifications[EVENT_ID];
      while (mask != 0) {
        const std::uint32_t BIT = mask & (~mask + 1); // Isolate lowest set bit
        const std::uint32_t IDX = __builtin_ctz(mask);
        auto& note = iface.notifications[IDX];
        invokeNotification(note, EVENT_ID, note.invokeCount);
        ++iface.stats.notificationsInvoked;
        mask ^= BIT;
      }
    } else {
      // Fallback: linear scan for out-of-range event IDs
      for (auto& note : iface.notifications) {
        if (shouldNotify(note, EVENT_ID)) {
          invokeNotification(note, EVENT_ID, note.invokeCount);
          ++iface.stats.notificationsInvoked;
        }
      }
    }

    // Start matching resident sequences
    bool sequenceMatched = false;
    if (EVENT_ID > 0 && EVENT_ID < Config::EVENT_ID_LIMIT) {
      // Fast path: bitmask lookup
      std::uint16_t mask = iface.eventToSequences[EVENT_ID];
      while (mask != 0) {
        const std::uint16_t BIT = mask & (~mask + 1);
        const std::uint16_t IDX = static_cast<std::uint16_t>(__builtin_ctz(mask));
        auto& seq = iface.sequences[IDX];
        if (seq.armed && seq.eventId == EVENT_ID) {
          startSequence(seq);
          sequenceMatched = true;
        }
        mask ^= BIT;
      }
    } else {
      for (auto& seq : iface.sequences) {
        if (shouldTrigger(seq, EVENT_ID)) {
          startSequence(seq);
          sequenceMatched = true;
        }
      }
    }

    // If no resident sequence matched, try catalog-based loading
    if (!sequenceMatched && iface.eventSequenceHandler) {
      iface.eventSequenceHandler(EVENT_ID);
    }

    // Activate ON_EVENT actions in the queue
    for (std::uint8_t a = 0; a < iface.actionCount; ++a) {
      auto& action = iface.actions[a];
      if (action.trigger == ActionTrigger::ON_EVENT && action.status == ActionStatus::PENDING &&
          action.triggerParam == EVENT_ID) {
        action.status = ActionStatus::ACTIVE;
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
inline std::uint16_t tickSingleSequence(ActionInterface& iface, DataSequence& seq,
                                        std::uint32_t currentCycle) noexcept {
  if (seq.currentStep >= seq.stepCount) {
    seq.status = SequenceStatus::COMPLETE;
    return 0;
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
        return 0;
      }
    }

    if (seq.delayRemaining > 0) {
      return 0; // Still counting down
    }

    // Delay done. Transition to wait condition or executing.
    if (step.waitCondition.enabled) {
      seq.status = SequenceStatus::WAITING_CONDITION;
      return 0;
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
        return 0;
      }
    }

    // Resolve condition target and evaluate
    std::size_t dataLen = 0;
    std::uint8_t* data = resolveTarget(iface, step.waitCondition.target, dataLen);
    if (data == nullptr || !evaluateStepCondition(step.waitCondition, data, dataLen)) {
      return 0; // Condition not met yet
    }
    seq.status = SequenceStatus::EXECUTING;
    // Fall through to EXECUTING dispatch below
  }

  /* ---- EXECUTING: dispatch action and advance ---- */
  if (seq.status == SequenceStatus::EXECUTING) {
    if (enqueueStepAction(iface, step.action)) {
      ++iface.stats.sequenceSteps;
    }

    std::uint16_t chainId = 0;
    advanceStep(seq, chainId);
    return chainId;
  }

  return 0;
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

    const std::uint16_t CHAIN_ID = tickSingleSequence(iface, seq, currentCycle);

    // Handle START_RTS: start another sequence by ID via catalog
    if (CHAIN_ID != 0) {
      if (iface.chainSequenceHandler) {
        // Catalog-based: lookup by sequenceId + on-demand load
        iface.chainSequenceHandler(CHAIN_ID);
      } else {
        // Fallback: try to find a resident sequence with matching sequenceId
        for (auto& target : iface.sequences) {
          if (target.armed && target.sequenceId == CHAIN_ID) {
            const std::uint64_t START_REF = (target.type == SequenceType::ATS && iface.timeProvider)
                                                ? iface.timeProvider()
                                                : static_cast<std::uint64_t>(currentCycle);
            startSequence(target, START_REF);
            break;
          }
        }
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
 *   - ACTIVE: Dispatch the action, then expire.
 *   - EXPIRED/SUCCESS: Remove from queue.
 *
 * All actions are one-shot: they fire once and are removed.
 */
inline void processActions(ActionInterface& iface, std::uint32_t currentCycle) noexcept {
  std::uint8_t i = 0;
  while (i < iface.actionCount) {
    auto& action = iface.actions[i];

    // Check trigger for pending actions
    if (action.status == ActionStatus::PENDING) {
      if (shouldActivate(action, currentCycle)) {
        action.status = ActionStatus::ACTIVE;
      } else {
        ++i;
        continue;
      }
    }

    // Process active actions
    if (action.status == ActionStatus::ACTIVE) {
      switch (action.actionType) {
      case ActionType::COMMAND:
        if (iface.commandHandler) {
          iface.commandHandler(action.target.fullUid, action.commandOpcode,
                               action.commandPayload.data(), action.commandPayloadLen);
          ++iface.stats.commandsRouted;
        }
        break;

      case ActionType::ARM_CONTROL:
        applyArmControl(iface, action);
        ++iface.stats.armControlsApplied;
        break;
      }

      // All actions are one-shot
      action.status = ActionStatus::EXPIRED;
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

/* ----------------------------- Abort Event Dispatch ----------------------------- */

/**
 * @brief Dispatch pending abort events from sequences that were aborted this cycle.
 * @param iface ActionInterface to process.
 * @note RT-safe: O(SEQUENCE_TABLE_SIZE * (NOTIFICATION_TABLE_SIZE + SEQUENCE_TABLE_SIZE)).
 *
 * Scans all sequence slots for abortEventPending flags set by abortSequence()
 * or applyTimeoutPolicy(). Dispatches each abort event through the same
 * notification and sequence trigger pipeline as regular events.
 */
inline void dispatchAbortEvents(ActionInterface& iface) noexcept {
  // Collect abort events (bounded by sequence table size)
  std::array<std::uint16_t, SEQUENCE_TABLE_SIZE> abortEvents{};
  std::uint8_t abortCount = 0;

  for (auto& seq : iface.sequences) {
    if (seq.abortEventPending) {
      seq.abortEventPending = false;
      if (abortCount < SEQUENCE_TABLE_SIZE) {
        abortEvents[abortCount] = seq.abortEventId;
        ++abortCount;
      }
    }
  }

  if (abortCount > 0) {
    iface.stats.abortEventsDispatched += abortCount;
    dispatchEvents(iface, abortEvents.data(), abortCount);
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
  std::uint8_t eventCount =
      evaluateWatchpoints(iface, firedEvents.data(), MAX_EVENTS, currentCycle);

  // 2. Evaluate groups
  eventCount = evaluateGroups(iface, firedEvents.data(), eventCount, MAX_EVENTS);

  // 3. Dispatch events
  if (eventCount > 0) {
    dispatchEvents(iface, firedEvents.data(), eventCount);
  }

  // 4. Tick sequences
  tickSequences(iface, currentCycle);

  // 5. Dispatch abort events (from preemption, stop, or timeout this cycle)
  dispatchAbortEvents(iface);

  // 6. Process actions
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
  iface.wpIdToIndex.fill(0xFF);
  iface.eventToNotifications.fill(0);
  iface.eventToSequences.fill(0);
}

} // namespace data
} // namespace system_core

#endif // APEX_SYSTEM_CORE_DATA_ACTION_INTERFACE_HPP
