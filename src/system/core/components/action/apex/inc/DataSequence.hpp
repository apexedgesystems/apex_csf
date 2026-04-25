#ifndef APEX_SYSTEM_CORE_DATA_SEQUENCE_HPP
#define APEX_SYSTEM_CORE_DATA_SEQUENCE_HPP
/**
 * @file DataSequence.hpp
 * @brief Ordered action lists triggered by watchpoint events.
 *
 * A DataSequence is a fixed-capacity list of SequenceSteps that execute
 * in order when an event fires. Each step pairs a DataAction with an
 * optional delay (in scheduler cycles). Steps execute one at a time:
 * when a step's delay elapses, the action fires and the sequence advances.
 *
 * Two execution modes:
 *   - RTS (Real-Time Sequence): Delays are relative cycle counts between
 *     steps. Started by event or ground command, runs at system rate.
 *   - ATS (Absolute-Time Sequence): Delays are absolute cycle offsets from
 *     sequence start. The engine computes targetCycle = startCycle +
 *     step.delayCycles and fires when currentCycle >= targetCycle.
 *
 * Per-step features:
 *   - Timeout: max wait cycles before policy fires (ABORT/SKIP/GOTO).
 *   - Wait condition: embedded lightweight watchpoint that blocks step
 *     advancement until a data condition is met.
 *   - Retry: on timeout with SKIP policy, retry the step up to retryMax
 *     times before actually skipping.
 *   - Branching: onComplete can advance linearly (NEXT), jump to a step
 *     (GOTO_STEP), or start another sequence by slot index (START_RTS).
 *
 * Connection to watchpoints:
 *   A watchpoint or group fires an eventId. Sequences bound to that eventId
 *   begin executing from step 0.
 *
 * RT-safe: All operations bounded, noexcept, no allocation.
 *
 * Usage:
 * @code
 *   // Build a 2-step NOOP sweep: command component A, wait 100 cycles,
 *   // command component B with 200-cycle timeout.
 *   DataSequence seq{};
 *   seq.type = SequenceType::RTS;
 *   seq.eventId = 5;
 *
 *   initCommandAction(seq.steps[0].action, 0x007800,
 *                     ActionTrigger::IMMEDIATE, 0, 0x0000);
 *   seq.steps[0].delayCycles = 100;
 *   seq.steps[0].timeoutCycles = 200;
 *   seq.steps[0].onTimeout = StepTimeoutPolicy::SKIP;
 *
 *   initCommandAction(seq.steps[1].action, 0x007A00,
 *                     ActionTrigger::IMMEDIATE, 0, 0x0000);
 *   seq.steps[1].delayCycles = 0;
 *
 *   seq.stepCount = 2;
 *   seq.armed = true;
 * @endcode
 */

#include "src/system/core/components/action/apex/inc/DataAction.hpp"
#include "src/system/core/components/action/apex/inc/DataWatchpoint.hpp"

#include <array>
#include <cstdint>

namespace system_core {
namespace data {

/* ----------------------------- Constants ----------------------------- */

/// Maximum steps per sequence.
constexpr std::size_t SEQUENCE_MAX_STEPS = Config::SEQUENCE_MAX_STEPS;

/// Maximum concurrent sequence execution slots (RTS + ATS combined).
constexpr std::size_t SEQUENCE_TABLE_SIZE = Config::RTS_SLOT_COUNT + Config::ATS_SLOT_COUNT;

/* ----------------------------- SequenceType ----------------------------- */

/**
 * @enum SequenceType
 * @brief Execution timing model for a sequence.
 */
enum class SequenceType : std::uint8_t {
  RTS = 0, ///< Real-Time Sequence: delays are relative cycle counts.
  ATS = 1  ///< Absolute-Time Sequence: delays are absolute cycle offsets from start.
};

/**
 * @brief Human-readable string for SequenceType.
 * @param t Type value.
 * @return Static string.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline const char* toString(SequenceType t) noexcept {
  switch (t) {
  case SequenceType::RTS:
    return "RTS";
  case SequenceType::ATS:
    return "ATS";
  }
  return "UNKNOWN";
}

/* ----------------------------- SequenceStatus ----------------------------- */

/**
 * @enum SequenceStatus
 * @brief Lifecycle state of a sequence.
 */
enum class SequenceStatus : std::uint8_t {
  IDLE = 0,              ///< Not running, waiting for event.
  WAITING = 1,           ///< Current step delay counting down.
  EXECUTING = 2,         ///< Current step action is active.
  COMPLETE = 3,          ///< All steps finished.
  WAITING_CONDITION = 4, ///< Delay elapsed, waiting for step condition.
  TIMED_OUT = 5,         ///< Aborted due to step timeout.
  ABORTED = 6            ///< Aborted by ground command or error.
};

/**
 * @brief Human-readable string for SequenceStatus.
 * @param s Status value.
 * @return Static string.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline const char* toString(SequenceStatus s) noexcept {
  switch (s) {
  case SequenceStatus::IDLE:
    return "IDLE";
  case SequenceStatus::WAITING:
    return "WAITING";
  case SequenceStatus::EXECUTING:
    return "EXECUTING";
  case SequenceStatus::COMPLETE:
    return "COMPLETE";
  case SequenceStatus::WAITING_CONDITION:
    return "WAITING_CONDITION";
  case SequenceStatus::TIMED_OUT:
    return "TIMED_OUT";
  case SequenceStatus::ABORTED:
    return "ABORTED";
  }
  return "UNKNOWN";
}

/* ----------------------------- StepTimeoutPolicy ----------------------------- */

/**
 * @enum StepTimeoutPolicy
 * @brief What happens when a step's timeout expires.
 */
enum class StepTimeoutPolicy : std::uint8_t {
  ABORT = 0,    ///< Abort entire sequence (status -> TIMED_OUT).
  SKIP = 1,     ///< Skip to next step (or retry first if retryMax > 0).
  GOTO_STEP = 2 ///< Jump to gotoStep index.
};

/**
 * @brief Human-readable string for StepTimeoutPolicy.
 * @param p Policy value.
 * @return Static string.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline const char* toString(StepTimeoutPolicy p) noexcept {
  switch (p) {
  case StepTimeoutPolicy::ABORT:
    return "ABORT";
  case StepTimeoutPolicy::SKIP:
    return "SKIP";
  case StepTimeoutPolicy::GOTO_STEP:
    return "GOTO_STEP";
  }
  return "UNKNOWN";
}

/* ----------------------------- StepCompletionAction ----------------------------- */

/**
 * @enum StepCompletionAction
 * @brief What happens after a step executes successfully.
 */
enum class StepCompletionAction : std::uint8_t {
  NEXT = 0,      ///< Advance to next step (default linear execution).
  GOTO_STEP = 1, ///< Jump to gotoStep index.
  START_RTS = 2  ///< Start another sequence by slot index (gotoStep = slot).
};

/**
 * @brief Human-readable string for StepCompletionAction.
 * @param a Action value.
 * @return Static string.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline const char* toString(StepCompletionAction a) noexcept {
  switch (a) {
  case StepCompletionAction::NEXT:
    return "NEXT";
  case StepCompletionAction::GOTO_STEP:
    return "GOTO_STEP";
  case StepCompletionAction::START_RTS:
    return "START_RTS";
  }
  return "UNKNOWN";
}

/* ----------------------------- StepWaitCondition ----------------------------- */

/**
 * @struct StepWaitCondition
 * @brief Lightweight embedded watchpoint for per-step data condition checks.
 *
 * When enabled, the sequence engine holds the step in WAITING_CONDITION
 * state after the delay elapses, evaluating the condition each tick until
 * it becomes true or the step timeout fires.
 *
 * Uses the same resolver delegate as regular watchpoints to look up live
 * data by (fullUid, category, byteOffset).
 *
 * @note RT-safe: Pure POD, no allocation.
 */
struct StepWaitCondition {
  DataTarget target{};                                    ///< What to watch.
  WatchPredicate predicate{WatchPredicate::EQ};           ///< When satisfied.
  WatchDataType dataType{WatchDataType::RAW};             ///< How to interpret bytes.
  std::array<std::uint8_t, WATCH_VALUE_SIZE> threshold{}; ///< Comparison value.
  bool enabled{false};                                    ///< Active flag.
};

/* ----------------------------- SequenceStep ----------------------------- */

/**
 * @struct SequenceStep
 * @brief A single step in a DataSequence with full control flow.
 *
 * Extends the basic action + delay model with:
 *   - Timeout with configurable policy (ABORT/SKIP/GOTO).
 *   - Wait condition (embedded watchpoint that blocks advancement).
 *   - Retry on timeout (retryMax attempts before policy applies).
 *   - Branching on completion (NEXT/GOTO_STEP/START_RTS).
 *
 * For RTS: delayCycles is relative to previous step completion.
 * For ATS: delayCycles is absolute cycle offset from sequence start.
 *
 * @note RT-safe: Pure POD, no allocation.
 */
struct SequenceStep {
  DataAction action{};          ///< Action to execute when step fires.
  std::uint32_t delayCycles{0}; ///< Delay before this step (RTS: relative, ATS: absolute).

  /* ---- Timeout ---- */
  std::uint32_t timeoutCycles{0};                        ///< Max wait (0 = no timeout).
  StepTimeoutPolicy onTimeout{StepTimeoutPolicy::ABORT}; ///< Policy on timeout.

  /* ---- Completion ---- */
  StepCompletionAction onComplete{StepCompletionAction::NEXT}; ///< Post-execution action.
  std::uint8_t gotoStep{0};       ///< Target for GOTO_STEP (step index within sequence).
  std::uint16_t chainTargetId{0}; ///< Target for START_RTS (sequence ID, resolved via catalog).

  /* ---- Retry ---- */
  std::uint8_t retryMax{0};   ///< Max retries on timeout (0 = no retry).
  std::uint8_t retryCount{0}; ///< Current retry count (runtime state).

  /* ---- Wait Condition ---- */
  StepWaitCondition waitCondition{}; ///< Optional hold condition.

  /* ---- Runtime State ---- */
  std::uint32_t timeoutRemaining{0}; ///< Runtime countdown for timeout.
};

/* ----------------------------- DataSequence ----------------------------- */

/**
 * @struct DataSequence
 * @brief Ordered list of steps triggered by a watchpoint event.
 *
 * When the bound eventId fires, the sequence begins at step 0. Each step
 * waits its delay, optionally checks a wait condition, then fires its
 * action. After the last step completes, the sequence status becomes
 * COMPLETE (or restarts if repeatMax > 0).
 *
 * Repeat modes:
 *   - repeatMax = 0: Run once, then COMPLETE.
 *   - repeatMax = N: Run N+1 times total (1 initial + N repeats).
 *   - repeatMax = 0xFF: Loop forever until disarmed.
 *
 * Re-triggering while running restarts from step 0 (preemptive restart).
 *
 * @note RT-safe: No allocation. Fixed-size step array.
 */
/// Sentinel value for infinite repeats.
constexpr std::uint8_t SEQUENCE_REPEAT_FOREVER = 0xFF;

struct DataSequence {
  std::array<SequenceStep, SEQUENCE_MAX_STEPS> steps{}; ///< Step table.
  std::uint8_t stepCount{0};                            ///< Number of active steps.
  std::uint8_t currentStep{0};                          ///< Index of current step.
  SequenceType type{SequenceType::RTS};                 ///< Timing model.
  SequenceStatus status{SequenceStatus::IDLE};          ///< Current lifecycle state.
  std::uint16_t eventId{0};                             ///< Event that triggers this sequence.
  std::uint32_t delayRemaining{0};                      ///< Countdown for current step delay.
  bool armed{false};                                    ///< Active flag.
  std::uint32_t runCount{0};                            ///< Times this sequence has been started.
  std::uint8_t repeatMax{0};                            ///< Max repeats (0=once, 0xFF=forever).
  std::uint8_t repeatCount{0};                          ///< Current repeat iteration.
  std::uint16_t sequenceId{0};                          ///< Unique ID for logging/telemetry.
  std::uint64_t startTime{0}; ///< Start reference (cycles or microseconds for ATS).

  /* ---- Abort Event ---- */
  std::uint16_t abortEventId{0}; ///< Event to fire on abort/preemption (0 = none).
  bool abortEventPending{false}; ///< Set by abortSequence(), cleared after dispatch.
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Start a sequence from step 0.
 * @param seq Sequence to start (modified: status, currentStep, delayRemaining, runCount).
 * @param currentCycle Current scheduler cycle (captured as startCycle for ATS).
 * @note RT-safe: O(1).
 *
 * If already running, restarts from step 0 (preemptive restart).
 * Initializes timeout countdown for the first step if configured.
 */
inline void startSequence(DataSequence& seq, std::uint64_t startRef = 0) noexcept {
  if (!seq.armed || seq.stepCount == 0) {
    return;
  }

  seq.currentStep = 0;
  seq.startTime = startRef;

  auto& step = seq.steps[0];
  step.retryCount = 0;

  if (seq.type == SequenceType::ATS) {
    // ATS: delay is absolute offset from start. Step 0 with offset 0 fires immediately.
    seq.delayRemaining = step.delayCycles; // Will be compared as absolute
    seq.status = (step.delayCycles == 0) ? SequenceStatus::EXECUTING : SequenceStatus::WAITING;
  } else {
    seq.delayRemaining = step.delayCycles;
    seq.status = (seq.delayRemaining > 0) ? SequenceStatus::WAITING : SequenceStatus::EXECUTING;
  }

  // Initialize timeout for first step
  step.timeoutRemaining = step.timeoutCycles;

  ++seq.runCount;
}

/**
 * @brief Advance a sequence by one tick (cycle or millisecond).
 * @param seq Sequence to advance (modified: status, currentStep, delayRemaining).
 * @param[out] actionReady Set to true if the current step's action should fire.
 * @return Pointer to the action to execute, or nullptr if no action this tick.
 * @note RT-safe: O(1).
 *
 * Call once per scheduler cycle (RTS) or once per ms (ATS). When a step's
 * delay reaches zero, returns the step's action pointer and sets actionReady.
 * After the action fires, call advanceStep() to move to the next step.
 */
[[nodiscard]] inline const DataAction* tickSequence(DataSequence& seq, bool& actionReady) noexcept {
  actionReady = false;

  if (seq.status == SequenceStatus::IDLE || seq.status == SequenceStatus::COMPLETE ||
      seq.status == SequenceStatus::TIMED_OUT || seq.status == SequenceStatus::ABORTED) {
    return nullptr;
  }

  if (seq.currentStep >= seq.stepCount) {
    seq.status = SequenceStatus::COMPLETE;
    return nullptr;
  }

  if (seq.status == SequenceStatus::WAITING) {
    if (seq.delayRemaining > 0) {
      --seq.delayRemaining;
    }
    if (seq.delayRemaining == 0) {
      // Delay done. Check for wait condition.
      auto& step = seq.steps[seq.currentStep];
      if (step.waitCondition.enabled) {
        seq.status = SequenceStatus::WAITING_CONDITION;
      } else {
        seq.status = SequenceStatus::EXECUTING;
        actionReady = true;
        return &step.action;
      }
    }
    return nullptr;
  }

  // EXECUTING and WAITING_CONDITION: handled by the engine (tickSequences)
  return nullptr;
}

/**
 * @brief Apply timeout policy to a sequence step.
 * @param seq Sequence to modify.
 * @return True if sequence should continue (SKIP/GOTO), false if aborted.
 * @note RT-safe: O(1).
 */
inline bool applyTimeoutPolicy(DataSequence& seq) noexcept {
  if (seq.currentStep >= seq.stepCount) {
    seq.status = SequenceStatus::TIMED_OUT;
    if (seq.abortEventId != 0) {
      seq.abortEventPending = true;
    }
    return false;
  }

  auto& step = seq.steps[seq.currentStep];
  const auto POLICY = step.onTimeout;

  // SKIP with retry: restart the step instead of skipping
  if (POLICY == StepTimeoutPolicy::SKIP && step.retryCount < step.retryMax) {
    ++step.retryCount;
    seq.delayRemaining = step.delayCycles;
    step.timeoutRemaining = step.timeoutCycles;
    seq.status = (seq.delayRemaining > 0) ? SequenceStatus::WAITING : SequenceStatus::EXECUTING;
    return true;
  }

  switch (POLICY) {
  case StepTimeoutPolicy::ABORT:
    seq.status = SequenceStatus::TIMED_OUT;
    if (seq.abortEventId != 0) {
      seq.abortEventPending = true;
    }
    return false;

  case StepTimeoutPolicy::SKIP:
    // Retries exhausted or no retries configured. Skip to next step.
    ++seq.currentStep;
    if (seq.currentStep >= seq.stepCount) {
      seq.status = SequenceStatus::COMPLETE;
      return false;
    }
    seq.steps[seq.currentStep].retryCount = 0;
    seq.delayRemaining = seq.steps[seq.currentStep].delayCycles;
    seq.steps[seq.currentStep].timeoutRemaining = seq.steps[seq.currentStep].timeoutCycles;
    seq.status = (seq.delayRemaining > 0) ? SequenceStatus::WAITING : SequenceStatus::EXECUTING;
    return true;

  case StepTimeoutPolicy::GOTO_STEP:
    if (step.gotoStep >= seq.stepCount) {
      seq.status = SequenceStatus::TIMED_OUT;
      if (seq.abortEventId != 0) {
        seq.abortEventPending = true;
      }
      return false;
    }
    seq.currentStep = step.gotoStep;
    seq.steps[seq.currentStep].retryCount = 0;
    seq.delayRemaining = seq.steps[seq.currentStep].delayCycles;
    seq.steps[seq.currentStep].timeoutRemaining = seq.steps[seq.currentStep].timeoutCycles;
    seq.status = (seq.delayRemaining > 0) ? SequenceStatus::WAITING : SequenceStatus::EXECUTING;
    return true;
  }

  seq.status = SequenceStatus::TIMED_OUT;
  if (seq.abortEventId != 0) {
    seq.abortEventPending = true;
  }
  return false;
}

/**
 * @brief Advance to the next step after the current action completes.
 * @param seq Sequence to advance (modified: currentStep, delayRemaining, status).
 * @param[out] chainId Set to sequence ID if onComplete is START_RTS, else 0.
 * @note RT-safe: O(1).
 *
 * Applies the step's onComplete action: NEXT (linear), GOTO_STEP (branch),
 * or START_RTS (trigger another sequence by ID via catalog). For START_RTS,
 * the caller is responsible for starting the target sequence.
 */
inline void advanceStep(DataSequence& seq, std::uint16_t& chainId) noexcept {
  chainId = 0;

  if (seq.status != SequenceStatus::EXECUTING) {
    return;
  }

  const auto& STEP = seq.steps[seq.currentStep];
  const auto ON_COMPLETE = STEP.onComplete;

  // Determine next step based on completion action
  std::uint8_t nextStep = 0;
  switch (ON_COMPLETE) {
  case StepCompletionAction::NEXT:
    nextStep = seq.currentStep + 1;
    break;
  case StepCompletionAction::GOTO_STEP:
    nextStep = STEP.gotoStep;
    break;
  case StepCompletionAction::START_RTS:
    chainId = STEP.chainTargetId;
    nextStep = seq.currentStep + 1;
    break;
  }

  seq.currentStep = nextStep;

  if (seq.currentStep >= seq.stepCount) {
    // Check if we should loop
    if (seq.repeatMax == SEQUENCE_REPEAT_FOREVER || seq.repeatCount < seq.repeatMax) {
      ++seq.repeatCount;
      seq.currentStep = 0;
      auto& step = seq.steps[0];
      step.retryCount = 0;
      seq.delayRemaining = step.delayCycles;
      step.timeoutRemaining = step.timeoutCycles;
      seq.status = (seq.delayRemaining > 0) ? SequenceStatus::WAITING : SequenceStatus::EXECUTING;
      ++seq.runCount;
      return;
    }
    seq.status = SequenceStatus::COMPLETE;
    return;
  }

  auto& nextStepRef = seq.steps[seq.currentStep];
  nextStepRef.retryCount = 0;
  seq.delayRemaining = nextStepRef.delayCycles;
  nextStepRef.timeoutRemaining = nextStepRef.timeoutCycles;
  seq.status = (seq.delayRemaining > 0) ? SequenceStatus::WAITING : SequenceStatus::EXECUTING;
}

/**
 * @brief Advance to the next step (overload without chainId output).
 * @param seq Sequence to advance.
 * @note RT-safe: O(1).
 */
inline void advanceStep(DataSequence& seq) noexcept {
  std::uint16_t chainId = 0;
  advanceStep(seq, chainId);
}

/**
 * @brief Reset a sequence to IDLE state.
 * @param seq Sequence to reset (modified: status, currentStep, delayRemaining).
 * @note RT-safe: O(1).
 */
inline void resetSequence(DataSequence& seq) noexcept {
  seq.status = SequenceStatus::IDLE;
  seq.currentStep = 0;
  seq.delayRemaining = 0;
  seq.repeatCount = 0;
}

/**
 * @brief Check if a sequence should be triggered by the given event ID.
 * @param seq Sequence to check.
 * @param eventId Event ID that fired.
 * @return True if the sequence is armed and bound to this event.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool shouldTrigger(const DataSequence& seq, std::uint16_t eventId) noexcept {
  return seq.armed && seq.eventId == eventId;
}

/**
 * @brief Check if a sequence has finished all steps.
 * @param seq Sequence to check.
 * @return True if status is COMPLETE.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool isComplete(const DataSequence& seq) noexcept {
  return seq.status == SequenceStatus::COMPLETE;
}

/**
 * @brief Check if a sequence is actively running.
 * @param seq Sequence to check.
 * @return True if status is WAITING, EXECUTING, or WAITING_CONDITION.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool isRunning(const DataSequence& seq) noexcept {
  // Bitmask check: running states are WAITING(1), EXECUTING(2), WAITING_CONDITION(4)
  // Single shift+AND instead of 3 enum comparisons.
  constexpr std::uint8_t RUNNING_MASK =
      (1u << static_cast<std::uint8_t>(SequenceStatus::WAITING)) |
      (1u << static_cast<std::uint8_t>(SequenceStatus::EXECUTING)) |
      (1u << static_cast<std::uint8_t>(SequenceStatus::WAITING_CONDITION));
  return (RUNNING_MASK >> static_cast<std::uint8_t>(seq.status)) & 1u;
}

/**
 * @brief Abort a running sequence.
 * @param seq Sequence to abort (modified: status).
 * @note RT-safe: O(1).
 */
inline void abortSequence(DataSequence& seq) noexcept {
  if (isRunning(seq)) {
    seq.status = SequenceStatus::ABORTED;
    if (seq.abortEventId != 0) {
      seq.abortEventPending = true;
    }
  }
}

/**
 * @brief Evaluate a step's wait condition against resolved data.
 * @param cond Wait condition to evaluate.
 * @param data Pointer to watched bytes (already offset-adjusted).
 * @param dataLen Number of bytes available.
 * @return True if condition is satisfied or not enabled.
 * @note RT-safe: O(byteLen).
 */
[[nodiscard]] inline bool evaluateStepCondition(const StepWaitCondition& cond,
                                                const std::uint8_t* data,
                                                std::size_t dataLen) noexcept {
  if (!cond.enabled || data == nullptr || dataLen == 0) {
    return true;
  }

  // Build a temporary watchpoint for evaluation
  DataWatchpoint wp{};
  wp.target = cond.target;
  wp.predicate = cond.predicate;
  wp.dataType = cond.dataType;
  wp.threshold = cond.threshold;
  wp.armed = true;

  return evaluatePredicate(wp, data, dataLen);
}

} // namespace data
} // namespace system_core

#endif // APEX_SYSTEM_CORE_DATA_SEQUENCE_HPP
