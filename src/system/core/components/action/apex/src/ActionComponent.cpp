/**
 * @file ActionComponent.cpp
 * @brief ActionComponent lifecycle and TPRM loading implementation.
 */

#include "src/system/core/components/action/apex/inc/ActionComponent.hpp"
#include "src/system/core/components/action/apex/inc/ActionEngineTprm.hpp"
#include "src/system/core/infrastructure/data/inc/SequenceValidation.hpp"

#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"
#include "src/utilities/helpers/inc/Files.hpp"

#include <cstdio>
#include <cstring>

#include <filesystem>

#include <fmt/format.h>

namespace system_core {
namespace action {

/* ----------------------------- Lifecycle Hooks ----------------------------- */

std::uint8_t ActionComponent::doInit() noexcept {
  if (!iface_.resolver) {
    setLastError("ActionComponent requires a resolver delegate");
    return static_cast<std::uint8_t>(Status::ERROR_NO_RESOLVER);
  }
  return static_cast<std::uint8_t>(Status::SUCCESS);
}

void ActionComponent::doReset() noexcept { data::resetInterface(iface_); }

/* ----------------------------- TPRM Loading ----------------------------- */

bool ActionComponent::loadTprm(const std::filesystem::path& tprmDir) noexcept {
  if (!isRegistered()) {
    return false;
  }

  char filename[32];
  std::snprintf(filename, sizeof(filename), "%06x.tprm", fullUid());
  std::filesystem::path tprmPath = tprmDir / filename;

  if (!std::filesystem::exists(tprmPath)) {
    return false;
  }

  ActionEngineTprm loaded{};
  std::string error;
  if (!apex::helpers::files::hex2cpp(tprmPath.string(), loaded, error)) {
    auto* log = componentLog();
    if (log != nullptr) {
      log->warning(label(), 1, fmt::format("TPRM load failed: {}", error));
    }
    return false;
  }

  // Deserialize into live ActionInterface tables.
  deserializeTprm(loaded);
  setConfigured(true);

  auto* log = componentLog();
  if (log != nullptr) {
    // Count armed entries for summary.
    std::uint8_t wpCount = 0;
    std::uint8_t grpCount = 0;
    std::uint8_t seqCount = 0;
    std::uint8_t noteCount = 0;
    std::uint8_t actCount = 0;

    for (const auto& wp : loaded.watchpoints) {
      if (wp.armed != 0) {
        ++wpCount;
      }
    }
    for (const auto& grp : loaded.groups) {
      if (grp.armed != 0) {
        ++grpCount;
      }
    }
    for (const auto& seq : loaded.sequences) {
      if (seq.armed != 0) {
        ++seqCount;
      }
    }
    for (const auto& note : loaded.notifications) {
      if (note.armed != 0) {
        ++noteCount;
      }
    }
    for (const auto& act : loaded.actions) {
      if (act.actionType != 0 || act.trigger != 0) {
        ++actCount;
      }
    }

    log->info(label(), fmt::format("TPRM loaded: {} watchpoints, {} groups, "
                                   "{} sequences, {} notifications, {} timed actions",
                                   wpCount, grpCount, seqCount, noteCount, actCount));
  }

  return true;
}

/* ----------------------------- TPRM Deserialization ----------------------------- */

void ActionComponent::deserializeTprm(const ActionEngineTprm& tprm) noexcept {
  using data::ActionStatus;
  using data::ActionTrigger;
  using data::ActionType;
  using data::ArmControlTarget;
  using data::DataCategory;
  using data::DataTarget;
  using data::GroupLogic;
  using data::LogSeverity;
  using data::SequenceType;
  using data::WatchDataType;
  using data::WatchPredicate;

  // Watchpoints
  for (std::size_t i = 0; i < 8; ++i) {
    const auto& SRC = tprm.watchpoints[i];
    auto& dst = iface_.watchpoints[i];

    dst.target = {SRC.targetFullUid, static_cast<DataCategory>(SRC.targetCategory),
                  SRC.targetByteOffset, SRC.targetByteLen};
    dst.predicate = static_cast<WatchPredicate>(SRC.predicate);
    dst.dataType = static_cast<WatchDataType>(SRC.dataType);
    dst.eventId = SRC.eventId;
    dst.minFireCount = SRC.minFireCount;
    std::memcpy(dst.threshold.data(), SRC.threshold, sizeof(SRC.threshold));
    dst.armed = (SRC.armed != 0);
  }

  // Groups
  for (std::size_t i = 0; i < 4; ++i) {
    const auto& SRC = tprm.groups[i];
    auto& dst = iface_.groups[i];

    for (std::size_t j = 0; j < 4; ++j) {
      dst.indices[j] = SRC.refs[j];
    }
    dst.count = SRC.refCount;
    dst.logic = static_cast<GroupLogic>(SRC.logic);
    dst.eventId = SRC.eventId;
    dst.armed = (SRC.armed != 0);
  }

  // Sequences
  for (std::size_t i = 0; i < 4; ++i) {
    const auto& SRC = tprm.sequences[i];
    auto& dst = iface_.sequences[i];

    dst.eventId = SRC.eventId;
    dst.stepCount = SRC.stepCount;
    dst.repeatCount = SRC.repeatCount;
    dst.type = static_cast<SequenceType>(SRC.type);
    dst.armed = (SRC.armed != 0);

    for (std::size_t s = 0; s < SRC.stepCount && s < 8; ++s) {
      const auto& STEP_SRC = SRC.steps[s];
      auto& stepDst = dst.steps[s];

      stepDst.delayCycles = STEP_SRC.delayCycles;
      stepDst.action.target = {STEP_SRC.targetFullUid,
                               static_cast<DataCategory>(STEP_SRC.targetCategory),
                               STEP_SRC.targetByteOffset, STEP_SRC.targetByteLen};
      stepDst.action.actionType = static_cast<ActionType>(STEP_SRC.actionType);
      stepDst.action.trigger = ActionTrigger::IMMEDIATE;
      stepDst.action.status = ActionStatus::PENDING;
      stepDst.action.duration = STEP_SRC.duration;
      stepDst.action.cyclesRemaining = STEP_SRC.duration;
      std::memcpy(stepDst.action.andMask.data(), STEP_SRC.andMask, 8);
      std::memcpy(stepDst.action.xorMask.data(), STEP_SRC.xorMask, 8);

      // ARM_CONTROL fields
      stepDst.action.armTarget = static_cast<ArmControlTarget>(STEP_SRC.armTarget);
      stepDst.action.armIndex = STEP_SRC.armIndex;
      stepDst.action.armState = (STEP_SRC.armState != 0);
    }
  }

  // Notifications
  for (std::size_t i = 0; i < 8; ++i) {
    const auto& SRC = tprm.notifications[i];
    auto& dst = iface_.notifications[i];

    dst.eventId = SRC.eventId;
    dst.armed = (SRC.armed != 0);

    // Built-in log fields (no callback -- ActionComponent handles log dispatch)
    std::memcpy(dst.logLabel, SRC.logLabel, sizeof(dst.logLabel));
    std::memcpy(dst.logMessage, SRC.logMessage, sizeof(dst.logMessage));
    dst.logSeverity = static_cast<LogSeverity>(SRC.logSeverity);
  }

  // Timed actions
  std::uint8_t actionCount = 0;
  for (std::size_t i = 0; i < 16; ++i) {
    const auto& SRC = tprm.actions[i];

    // Skip empty entries (all zeros = unused slot).
    if (SRC.targetFullUid == 0 && SRC.triggerParam == 0 && SRC.actionType == 0 &&
        SRC.trigger == 0) {
      continue;
    }

    auto& dst = iface_.actions[actionCount];

    dst.target = {SRC.targetFullUid, static_cast<DataCategory>(SRC.targetCategory),
                  SRC.targetByteOffset, SRC.targetByteLen};
    dst.actionType = static_cast<ActionType>(SRC.actionType);
    dst.trigger = static_cast<ActionTrigger>(SRC.trigger);
    dst.status = ActionStatus::PENDING;
    dst.triggerParam = SRC.triggerParam;
    dst.duration = SRC.duration;
    dst.cyclesRemaining = SRC.duration;
    std::memcpy(dst.andMask.data(), SRC.andMask, 8);
    std::memcpy(dst.xorMask.data(), SRC.xorMask, 8);

    // ARM_CONTROL fields
    dst.armTarget = static_cast<ArmControlTarget>(SRC.armTarget);
    dst.armIndex = SRC.armIndex;
    dst.armState = (SRC.armState != 0);

    ++actionCount;
  }
  iface_.actionCount = actionCount;
}

/* ----------------------------- RTS/ATS Loading ----------------------------- */

bool ActionComponent::loadRts(std::uint8_t slot, const std::filesystem::path& path) noexcept {
  if (slot >= data::SEQUENCE_TABLE_SIZE) {
    return false;
  }
  if (!std::filesystem::exists(path)) {
    return false;
  }

  StandaloneSequenceTprm loaded{};
  std::string error;
  if (!apex::helpers::files::hex2cpp(path.string(), loaded, error)) {
    auto* log = componentLog();
    if (log != nullptr) {
      log->warning(label(), 1, fmt::format("RTS load failed: {}", error));
    }
    return false;
  }

  deserializeStandaloneSequence(slot, loaded, data::SequenceType::RTS);

  // Validate after deserialization
  data::SequenceError valErr{};
  if (!data::validateSequence(iface_.sequences[slot], &valErr)) {
    auto* log = componentLog();
    if (log != nullptr) {
      log->warning(label(), static_cast<std::uint8_t>(valErr.code),
                   fmt::format("RTS validation failed (slot={}): {}", slot, valErr.message));
    }
    iface_.sequences[slot] = data::DataSequence{}; // Clear invalid sequence
    return false;
  }

  ++iface_.stats.rtsLoaded;

  auto* log = componentLog();
  if (log != nullptr) {
    const std::uint16_t SEQ_ID = loaded.sequenceId;
    const std::uint8_t STEP_CT = loaded.stepCount;
    log->info(label(), fmt::format("RTS loaded: slot={} id={} steps={} file={}", slot, SEQ_ID,
                                   STEP_CT, path.filename().string()));
  }
  return true;
}

bool ActionComponent::loadAts(std::uint8_t slot, const std::filesystem::path& path) noexcept {
  if (slot >= data::SEQUENCE_TABLE_SIZE) {
    return false;
  }
  if (!std::filesystem::exists(path)) {
    return false;
  }

  StandaloneSequenceTprm loaded{};
  std::string error;
  if (!apex::helpers::files::hex2cpp(path.string(), loaded, error)) {
    auto* log = componentLog();
    if (log != nullptr) {
      log->warning(label(), 1, fmt::format("ATS load failed: {}", error));
    }
    return false;
  }

  deserializeStandaloneSequence(slot, loaded, data::SequenceType::ATS);

  // Validate structure
  data::SequenceError valErr{};
  if (!data::validateSequence(iface_.sequences[slot], &valErr)) {
    auto* log = componentLog();
    if (log != nullptr) {
      log->warning(label(), static_cast<std::uint8_t>(valErr.code),
                   fmt::format("ATS validation failed (slot={}): {}", slot, valErr.message));
    }
    iface_.sequences[slot] = data::DataSequence{};
    return false;
  }

  // Validate ATS timeline (warn if no time provider)
  if (!iface_.timeProvider) {
    auto* log = componentLog();
    if (log != nullptr) {
      log->warning(label(), 0,
                   fmt::format("ATS loaded (slot={}) but no time provider set -- "
                               "will use cycle-based fallback timing",
                               slot));
    }
  }

  ++iface_.stats.atsLoaded;

  auto* log = componentLog();
  if (log != nullptr) {
    const std::uint16_t SEQ_ID = loaded.sequenceId;
    const std::uint8_t STEP_CT = loaded.stepCount;
    log->info(label(), fmt::format("ATS loaded: slot={} id={} steps={} file={}", slot, SEQ_ID,
                                   STEP_CT, path.filename().string()));
  }
  return true;
}

/* ----------------------------- Standalone Deserialization ----------------------------- */

void ActionComponent::deserializeStandaloneSequence(std::uint8_t slot,
                                                    const StandaloneSequenceTprm& tprm,
                                                    data::SequenceType forceType) noexcept {
  using data::ActionStatus;
  using data::ActionTrigger;
  using data::ActionType;
  using data::ArmControlTarget;
  using data::DataCategory;
  using data::SequenceType;
  using data::StepCompletionAction;
  using data::StepTimeoutPolicy;
  using data::WatchDataType;
  using data::WatchPredicate;

  auto& dst = iface_.sequences[slot];
  dst = data::DataSequence{}; // Clear slot

  dst.sequenceId = tprm.sequenceId;
  dst.eventId = tprm.eventId;
  dst.stepCount = (tprm.stepCount > data::SEQUENCE_MAX_STEPS)
                      ? static_cast<std::uint8_t>(data::SEQUENCE_MAX_STEPS)
                      : tprm.stepCount;
  dst.repeatMax = tprm.repeatCount;
  dst.type = forceType;
  dst.armed = (tprm.armed != 0);

  for (std::size_t s = 0; s < dst.stepCount; ++s) {
    const auto& SRC = tprm.steps[s];
    auto& stepDst = dst.steps[s];

    // Action target
    stepDst.action.target = {SRC.targetFullUid, static_cast<DataCategory>(SRC.targetCategory),
                             SRC.targetByteOffset, SRC.targetByteLen};
    stepDst.action.actionType = static_cast<ActionType>(SRC.actionType);
    stepDst.action.trigger = ActionTrigger::IMMEDIATE;
    stepDst.action.status = ActionStatus::PENDING;
    stepDst.action.duration = SRC.duration;
    stepDst.action.cyclesRemaining = SRC.duration;

    // DATA_WRITE fields
    std::memcpy(stepDst.action.andMask.data(), SRC.andMask, 8);
    std::memcpy(stepDst.action.xorMask.data(), SRC.xorMask, 8);

    // ARM_CONTROL fields
    stepDst.action.armTarget = static_cast<ArmControlTarget>(SRC.armTarget);
    stepDst.action.armIndex = SRC.armIndex;
    stepDst.action.armState = (SRC.armState != 0);

    // COMMAND fields
    stepDst.action.commandOpcode = SRC.commandOpcode;
    stepDst.action.commandPayloadLen = SRC.commandPayloadLen;
    std::memcpy(stepDst.action.commandPayload.data(), SRC.commandPayload,
                data::COMMAND_PAYLOAD_MAX);

    // Timing
    stepDst.delayCycles = SRC.delayCycles;
    stepDst.timeoutCycles = SRC.timeoutCycles;

    // Control flow
    stepDst.onTimeout = static_cast<StepTimeoutPolicy>(SRC.onTimeout);
    stepDst.onComplete = static_cast<StepCompletionAction>(SRC.onComplete);
    stepDst.gotoStep = SRC.gotoStep;
    stepDst.retryMax = SRC.retryMax;

    // Wait condition
    const auto& COND = SRC.waitCondition;
    stepDst.waitCondition.target = {COND.targetFullUid,
                                    static_cast<DataCategory>(COND.targetCategory),
                                    COND.targetByteOffset, COND.targetByteLen};
    stepDst.waitCondition.predicate = static_cast<WatchPredicate>(COND.predicate);
    stepDst.waitCondition.dataType = static_cast<WatchDataType>(COND.dataType);
    std::memcpy(stepDst.waitCondition.threshold.data(), COND.threshold, data::WATCH_VALUE_SIZE);
    stepDst.waitCondition.enabled = (COND.enabled != 0);
  }
}

/* ----------------------------- Command Handling ----------------------------- */

std::uint8_t ActionComponent::handleCommand(std::uint16_t opcode,
                                            apex::compat::rospan<std::uint8_t> payload,
                                            std::vector<std::uint8_t>& response) noexcept {
  using system_component::CommandResult;

  // ActionComponent opcodes: 0x0500-0x05FF
  constexpr std::uint16_t LOAD_RTS = 0x0500;
  constexpr std::uint16_t START_RTS = 0x0501;
  constexpr std::uint16_t STOP_RTS = 0x0502;
  constexpr std::uint16_t LOAD_ATS = 0x0503;
  constexpr std::uint16_t START_ATS = 0x0504;
  constexpr std::uint16_t STOP_ATS = 0x0505;

  switch (opcode) {
  case START_RTS:
  case START_ATS: {
    if (payload.empty()) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    const std::uint8_t SLOT = payload[0];
    if (SLOT >= data::SEQUENCE_TABLE_SIZE) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT);
    }
    auto& seq = iface_.sequences[SLOT];
    if (!seq.armed || seq.stepCount == 0) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT);
    }

    // ATS: validate timeline before starting (catch stale plans)
    if (seq.type == data::SequenceType::ATS && iface_.timeProvider) {
      const std::uint64_t NOW = iface_.timeProvider();
      data::SequenceError valErr{};
      if (!data::validateAtsTimeline(seq, NOW, NOW, &valErr)) {
        auto* log = componentLog();
        if (log != nullptr) {
          log->error(label(), static_cast<std::uint8_t>(valErr.code),
                     fmt::format("ATS start REJECTED (slot={}): {}", SLOT, valErr.message));
        }
        return static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT);
      }
    }

    // Capture start reference
    const std::uint64_t START_REF =
        (seq.type == data::SequenceType::ATS && iface_.timeProvider) ? iface_.timeProvider() : 0;
    data::startSequence(seq, START_REF);
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case STOP_RTS:
  case STOP_ATS: {
    if (payload.empty()) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    const std::uint8_t SLOT = payload[0];
    if (SLOT >= data::SEQUENCE_TABLE_SIZE) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT);
    }
    data::abortSequence(iface_.sequences[SLOT]);
    data::resetSequence(iface_.sequences[SLOT]);
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case LOAD_RTS:
  case LOAD_ATS: {
    // Payload: u8 slot, char[N] filename (null-terminated)
    if (payload.size() < 2) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    const std::uint8_t SLOT = payload[0];
    if (SLOT >= data::SEQUENCE_TABLE_SIZE) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT);
    }

    // Extract path from payload[1..N] (null-terminated or full span)
    std::string filePath(reinterpret_cast<const char*>(payload.data() + 1), payload.size() - 1);
    const auto NULL_POS = filePath.find('\0');
    if (NULL_POS != std::string::npos) {
      filePath.resize(NULL_POS);
    }

    const bool IS_RTS = (opcode == LOAD_RTS);
    const bool OK = IS_RTS ? loadRts(SLOT, filePath) : loadAts(SLOT, filePath);
    if (!OK) {
      return static_cast<std::uint8_t>(CommandResult::LOAD_FAILED);
    }
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  default:
    // Delegate to base class for common opcodes
    return system_component::SystemComponentBase::handleCommand(opcode, payload, response);
  }
}

/* ----------------------------- Log Dispatch ----------------------------- */

void ActionComponent::dispatchLogNotifications(std::uint16_t eventId,
                                               std::uint32_t fireCount) noexcept {
  auto* log = componentLog();
  if (log == nullptr) {
    return;
  }

  for (auto& note : iface_.notifications) {
    if (!note.armed || note.eventId != eventId || !note.hasLogMessage()) {
      continue;
    }
    if (note.callback) {
      continue; // Callback-based notifications are handled by dispatchEvent().
    }

    switch (note.logSeverity) {
    case data::LogSeverity::WARNING:
      log->warning(note.logLabel, static_cast<std::uint8_t>(eventId),
                   fmt::format("{} (fires={})", note.logMessage, fireCount));
      break;
    case data::LogSeverity::ERR:
      log->error(note.logLabel, static_cast<std::uint8_t>(eventId),
                 fmt::format("{} (fires={})", note.logMessage, fireCount));
      break;
    case data::LogSeverity::INFO:
    default:
      log->info(note.logLabel, fmt::format("{} (fires={})", note.logMessage, fireCount));
      break;
    }
    ++note.invokeCount;
  }
}

} // namespace action
} // namespace system_core
