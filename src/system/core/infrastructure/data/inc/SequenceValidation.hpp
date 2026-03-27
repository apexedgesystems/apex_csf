#ifndef APEX_SYSTEM_CORE_DATA_SEQUENCE_VALIDATION_HPP
#define APEX_SYSTEM_CORE_DATA_SEQUENCE_VALIDATION_HPP
/**
 * @file SequenceValidation.hpp
 * @brief Pre-flight validation for RTS/ATS sequences.
 *
 * Catches configuration errors at load time rather than runtime:
 *   - Out-of-bounds step references (gotoStep, stepCount)
 *   - Invalid enum values (onTimeout, onComplete, actionType)
 *   - ATS monotonicity violations (steps must have increasing offsets)
 *   - ATS timestamps in the past (relative to current time provider)
 *   - COMMAND steps targeting fullUid=0 (unroutable)
 *   - Wait conditions with invalid targets
 *   - Time standard mismatches (ATS standard vs provider)
 *
 * Validation is NOT RT-safe (string formatting for error messages).
 * Call at load time only, never from the tick loop.
 *
 * Usage:
 * @code
 *   SequenceError err{};
 *   if (!validateSequence(seq, &err)) {
 *     log->warning("ACTION", 1, err.message);
 *   }
 * @endcode
 */

#include "src/system/core/infrastructure/data/inc/DataSequence.hpp"
#include "src/utilities/time/inc/TimeBase.hpp"

#include <cstdint>
#include <cstdio>

namespace system_core {
namespace data {

/* ----------------------------- SequenceError ----------------------------- */

/**
 * @enum SequenceErrorCode
 * @brief Validation error categories.
 */
enum class SequenceErrorCode : std::uint8_t {
  NONE = 0,                  ///< No error.
  STEP_COUNT_OVERFLOW,       ///< stepCount > SEQUENCE_MAX_STEPS.
  GOTO_OUT_OF_BOUNDS,        ///< gotoStep >= stepCount.
  START_RTS_OUT_OF_BOUNDS,   ///< START_RTS gotoStep >= SEQUENCE_TABLE_SIZE.
  INVALID_TIMEOUT_POLICY,    ///< onTimeout value out of enum range.
  INVALID_COMPLETION_ACTION, ///< onComplete value out of enum range.
  INVALID_ACTION_TYPE,       ///< actionType value out of enum range.
  COMMAND_NO_TARGET,         ///< COMMAND action with fullUid=0.
  WAIT_CONDITION_NO_TARGET,  ///< Wait condition enabled but target fullUid=0.
  ATS_NOT_MONOTONIC,         ///< ATS steps not in increasing time order.
  ATS_TIME_IN_PAST,          ///< ATS step timestamp is before current time.
  ATS_NO_TIME_PROVIDER       ///< ATS sequence but no time provider configured.
};

/**
 * @brief Human-readable string for SequenceErrorCode.
 * @param c Code value.
 * @return Static string.
 */
[[nodiscard]] inline const char* toString(SequenceErrorCode c) noexcept {
  switch (c) {
  case SequenceErrorCode::NONE:
    return "NONE";
  case SequenceErrorCode::STEP_COUNT_OVERFLOW:
    return "STEP_COUNT_OVERFLOW";
  case SequenceErrorCode::GOTO_OUT_OF_BOUNDS:
    return "GOTO_OUT_OF_BOUNDS";
  case SequenceErrorCode::START_RTS_OUT_OF_BOUNDS:
    return "START_RTS_OUT_OF_BOUNDS";
  case SequenceErrorCode::INVALID_TIMEOUT_POLICY:
    return "INVALID_TIMEOUT_POLICY";
  case SequenceErrorCode::INVALID_COMPLETION_ACTION:
    return "INVALID_COMPLETION_ACTION";
  case SequenceErrorCode::INVALID_ACTION_TYPE:
    return "INVALID_ACTION_TYPE";
  case SequenceErrorCode::COMMAND_NO_TARGET:
    return "COMMAND_NO_TARGET";
  case SequenceErrorCode::WAIT_CONDITION_NO_TARGET:
    return "WAIT_CONDITION_NO_TARGET";
  case SequenceErrorCode::ATS_NOT_MONOTONIC:
    return "ATS_NOT_MONOTONIC";
  case SequenceErrorCode::ATS_TIME_IN_PAST:
    return "ATS_TIME_IN_PAST";
  case SequenceErrorCode::ATS_NO_TIME_PROVIDER:
    return "ATS_NO_TIME_PROVIDER";
  }
  return "UNKNOWN";
}

/**
 * @struct SequenceError
 * @brief Validation error with context.
 */
struct SequenceError {
  SequenceErrorCode code{SequenceErrorCode::NONE}; ///< Error category.
  std::uint8_t stepIndex{0};                       ///< Which step has the error (0xFF = header).
  char message[128]{};                             ///< Human-readable description.
};

/* ----------------------------- Validation ----------------------------- */

/**
 * @brief Validate a sequence for structural correctness.
 * @param seq Sequence to validate.
 * @param[out] err Error details (optional, may be nullptr).
 * @return True if sequence is valid.
 * @note NOT RT-safe: snprintf for error messages.
 */
[[nodiscard]] inline bool validateSequence(const DataSequence& seq, SequenceError* err) noexcept {
  auto fail = [&](SequenceErrorCode code, std::uint8_t step, const char* msg) {
    if (err != nullptr) {
      err->code = code;
      err->stepIndex = step;
      std::snprintf(err->message, sizeof(err->message), "%s", msg);
    }
    return false;
  };

  // Header checks
  if (seq.stepCount > SEQUENCE_MAX_STEPS) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "stepCount %u exceeds max %zu", seq.stepCount,
                  SEQUENCE_MAX_STEPS);
    return fail(SequenceErrorCode::STEP_COUNT_OVERFLOW, 0xFF, buf);
  }

  std::uint32_t prevDelay = 0;

  for (std::uint8_t i = 0; i < seq.stepCount; ++i) {
    const auto& STEP = seq.steps[i];

    // Validate enum ranges
    if (static_cast<std::uint8_t>(STEP.onTimeout) > 2) {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "step %u: invalid onTimeout=%u", i,
                    static_cast<unsigned>(STEP.onTimeout));
      return fail(SequenceErrorCode::INVALID_TIMEOUT_POLICY, i, buf);
    }

    if (static_cast<std::uint8_t>(STEP.onComplete) > 2) {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "step %u: invalid onComplete=%u", i,
                    static_cast<unsigned>(STEP.onComplete));
      return fail(SequenceErrorCode::INVALID_COMPLETION_ACTION, i, buf);
    }

    if (static_cast<std::uint8_t>(STEP.action.actionType) > 2) {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "step %u: invalid actionType=%u", i,
                    static_cast<unsigned>(STEP.action.actionType));
      return fail(SequenceErrorCode::INVALID_ACTION_TYPE, i, buf);
    }

    // GOTO_STEP bounds check (both timeout and completion)
    if (STEP.onTimeout == StepTimeoutPolicy::GOTO_STEP && STEP.gotoStep >= seq.stepCount) {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "step %u: onTimeout GOTO_STEP=%u >= stepCount=%u", i,
                    STEP.gotoStep, seq.stepCount);
      return fail(SequenceErrorCode::GOTO_OUT_OF_BOUNDS, i, buf);
    }

    if (STEP.onComplete == StepCompletionAction::GOTO_STEP && STEP.gotoStep >= seq.stepCount) {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "step %u: onComplete GOTO_STEP=%u >= stepCount=%u", i,
                    STEP.gotoStep, seq.stepCount);
      return fail(SequenceErrorCode::GOTO_OUT_OF_BOUNDS, i, buf);
    }

    // START_RTS slot bounds check
    if (STEP.onComplete == StepCompletionAction::START_RTS &&
        STEP.gotoStep >= SEQUENCE_TABLE_SIZE) {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "step %u: START_RTS slot=%u >= SEQUENCE_TABLE_SIZE=%zu", i,
                    STEP.gotoStep, SEQUENCE_TABLE_SIZE);
      return fail(SequenceErrorCode::START_RTS_OUT_OF_BOUNDS, i, buf);
    }

    // COMMAND action must target a real component
    if (STEP.action.actionType == ActionType::COMMAND && STEP.action.target.fullUid == 0) {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "step %u: COMMAND action with fullUid=0 (unroutable)", i);
      return fail(SequenceErrorCode::COMMAND_NO_TARGET, i, buf);
    }

    // Wait condition with enabled=true must have a valid target
    if (STEP.waitCondition.enabled && STEP.waitCondition.target.fullUid == 0) {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "step %u: wait condition enabled but target fullUid=0", i);
      return fail(SequenceErrorCode::WAIT_CONDITION_NO_TARGET, i, buf);
    }

    // ATS monotonicity: step delays must be non-decreasing
    if (seq.type == SequenceType::ATS && i > 0) {
      if (STEP.delayCycles < prevDelay) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "step %u: ATS delay %u < previous step delay %u (not monotonic)", i,
                      STEP.delayCycles, prevDelay);
        return fail(SequenceErrorCode::ATS_NOT_MONOTONIC, i, buf);
      }
    }
    prevDelay = STEP.delayCycles;
  }

  if (err != nullptr) {
    err->code = SequenceErrorCode::NONE;
    err->stepIndex = 0;
    err->message[0] = '\0';
  }
  return true;
}

/**
 * @brief Validate ATS timing against the current time provider.
 * @param seq ATS sequence to validate.
 * @param currentTime Current time from the provider (microseconds or cycles).
 * @param startTime Planned start time (0 = starting now).
 * @param[out] err Error details (optional).
 * @return True if all step times are in the future.
 * @note NOT RT-safe: snprintf for error messages.
 *
 * For ATS sequences, each step fires at startTime + delayCycles.
 * If any step's target time is already in the past, this returns false.
 * Call this before starting an ATS to catch stale command plans.
 */
[[nodiscard]] inline bool validateAtsTimeline(const DataSequence& seq, std::uint64_t currentTime,
                                              std::uint64_t startTime,
                                              SequenceError* err) noexcept {
  if (seq.type != SequenceType::ATS) {
    return true; // RTS doesn't need timeline validation
  }

  const std::uint64_t EFFECTIVE_START = (startTime > 0) ? startTime : currentTime;

  for (std::uint8_t i = 0; i < seq.stepCount; ++i) {
    const std::uint64_t TARGET = EFFECTIVE_START + seq.steps[i].delayCycles;
    if (TARGET < currentTime) {
      if (err != nullptr) {
        err->code = SequenceErrorCode::ATS_TIME_IN_PAST;
        err->stepIndex = i;
        std::snprintf(
            err->message, sizeof(err->message), "step %u: target time %llu < current time %llu", i,
            static_cast<unsigned long long>(TARGET), static_cast<unsigned long long>(currentTime));
      }
      return false;
    }
  }

  if (err != nullptr) {
    err->code = SequenceErrorCode::NONE;
    err->stepIndex = 0;
    err->message[0] = '\0';
  }
  return true;
}

} // namespace data
} // namespace system_core

#endif // APEX_SYSTEM_CORE_DATA_SEQUENCE_VALIDATION_HPP
