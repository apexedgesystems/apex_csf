/**
 * @file ActionComponent.cpp
 * @brief ActionComponent lifecycle and TPRM loading implementation.
 */

#include "src/system/core/components/action/apex/inc/ActionComponent.hpp"
#include "src/system/core/components/action/apex/inc/ActionEngineTprm.hpp"
#include "src/system/core/components/action/apex/inc/ActionTlm.hpp"
#include "src/system/core/components/action/apex/inc/SequenceValidation.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"
#include "src/utilities/helpers/inc/Files.hpp"

#include <cstdio>
#include <cstring>

#include <filesystem>

#include <fmt/format.h>

namespace system_core {
namespace action {

/* ----------------------------- Event Sequence Handler ----------------------------- */

/**
 * @brief Static handler for event-triggered catalog RTS loading.
 *
 * Called by ActionInterface::dispatchEvents when an event fires and no
 * resident sequence matches. Looks up the event in the catalog and starts
 * matching RTS sequences on demand.
 */
static void eventSequenceHandlerFn(void* ctx, std::uint16_t eventId) noexcept {
  auto* comp = static_cast<ActionComponent*>(ctx);
  comp->catalog().forEachByEvent(eventId, [comp](const data::CatalogEntry& entry) {
    if (entry.type == data::SequenceType::RTS && entry.binaryLoaded) {
      comp->startRtsById(entry.sequenceId);
    }
  });
}

/**
 * @brief Static handler for chain-triggered catalog RTS loading.
 *
 * Called by ActionInterface::tickSequences when a step completes with
 * START_RTS. Looks up the target sequence by ID and starts it.
 */
static void chainSequenceHandlerFn(void* ctx, std::uint16_t sequenceId) noexcept {
  auto* comp = static_cast<ActionComponent*>(ctx);
  comp->startRtsById(sequenceId);
}

/* ----------------------------- Lifecycle Hooks ----------------------------- */

std::uint8_t ActionComponent::doInit() noexcept {
  if (!iface_.resolver) {
    setLastError("ActionComponent requires a resolver delegate");
    return static_cast<std::uint8_t>(Status::ERROR_NO_RESOLVER);
  }

  // Wire catalog handlers for on-demand RTS loading
  iface_.eventSequenceHandler = {eventSequenceHandlerFn, this};
  iface_.chainSequenceHandler = {chainSequenceHandlerFn, this};

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
  data::rebuildWatchpointIndex(iface_);
  data::rebuildEventIndex(iface_);
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

  // Watchpoints (TPRM entries get 1-based IDs: index + 1)
  for (std::size_t i = 0; i < 8; ++i) {
    const auto& SRC = tprm.watchpoints[i];
    auto& dst = iface_.watchpoints[i];

    dst.watchpointId = static_cast<std::uint16_t>(i + 1);
    dst.target = {SRC.targetFullUid, static_cast<DataCategory>(SRC.targetCategory),
                  SRC.targetByteOffset, SRC.targetByteLen};
    dst.predicate = static_cast<WatchPredicate>(SRC.predicate);
    dst.dataType = static_cast<WatchDataType>(SRC.dataType);
    dst.eventId = SRC.eventId;
    dst.minFireCount = SRC.minFireCount;
    std::memcpy(dst.threshold.data(), SRC.threshold, sizeof(SRC.threshold));
    dst.armed = (SRC.armed != 0);
  }

  // Groups (refs are watchpoint IDs: TPRM refs are 0-based indices, convert to 1-based IDs)
  for (std::size_t i = 0; i < 4; ++i) {
    const auto& SRC = tprm.groups[i];
    auto& dst = iface_.groups[i];

    dst.groupId = static_cast<std::uint16_t>(i + 1);
    for (std::size_t j = 0; j < 4; ++j) {
      dst.refs[j] = static_cast<std::uint16_t>(SRC.refs[j] + 1);
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

    dst.notificationId = static_cast<std::uint16_t>(i + 1);
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

    // ARM_CONTROL fields
    dst.armTarget = static_cast<ArmControlTarget>(SRC.armTarget);
    dst.armIndex = SRC.armIndex;
    dst.armState = (SRC.armState != 0);

    ++actionCount;
  }
  iface_.actionCount = actionCount;

  // Populate resource catalogs from TPRM entries (enables runtime activate/deactivate)
  wpCatalog_.clear();
  for (std::size_t i = 0; i < 8; ++i) {
    const auto& SRC = tprm.watchpoints[i];
    if (SRC.targetFullUid == 0 && SRC.eventId == 0) {
      continue;
    }
    data::WatchpointDef def{};
    def.watchpointId = static_cast<std::uint16_t>(i + 1);
    def.target = {SRC.targetFullUid, static_cast<DataCategory>(SRC.targetCategory),
                  SRC.targetByteOffset, SRC.targetByteLen};
    def.predicate = static_cast<WatchPredicate>(SRC.predicate);
    def.dataType = static_cast<WatchDataType>(SRC.dataType);
    def.eventId = SRC.eventId;
    def.minFireCount = SRC.minFireCount;
    std::memcpy(def.threshold.data(), SRC.threshold, sizeof(SRC.threshold));
    def.activeOnBoot = (SRC.armed != 0);
    wpCatalog_.add(def);
  }

  grpCatalog_.clear();
  for (std::size_t i = 0; i < 4; ++i) {
    const auto& SRC = tprm.groups[i];
    if (SRC.eventId == 0 && SRC.refCount == 0) {
      continue;
    }
    data::GroupDef def{};
    def.groupId = static_cast<std::uint16_t>(i + 1);
    for (std::size_t j = 0; j < 4; ++j) {
      def.refs[j] = static_cast<std::uint16_t>(SRC.refs[j] + 1);
    }
    def.count = SRC.refCount;
    def.logic = static_cast<GroupLogic>(SRC.logic);
    def.eventId = SRC.eventId;
    def.activeOnBoot = (SRC.armed != 0);
    grpCatalog_.add(def);
  }

  noteCatalog_.clear();
  for (std::size_t i = 0; i < 8; ++i) {
    const auto& SRC = tprm.notifications[i];
    if (SRC.eventId == 0) {
      continue;
    }
    data::NotificationDef def{};
    def.notificationId = static_cast<std::uint16_t>(i + 1);
    def.eventId = SRC.eventId;
    def.severity = static_cast<LogSeverity>(SRC.logSeverity);
    std::memcpy(def.logLabel, SRC.logLabel, sizeof(def.logLabel));
    std::memcpy(def.logMessage, SRC.logMessage, sizeof(def.logMessage));
    def.activeOnBoot = (SRC.armed != 0);
    noteCatalog_.add(def);
  }
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

/* ----------------------------- Catalog Operations ----------------------------- */

std::size_t ActionComponent::scanCatalog(const std::filesystem::path& rtsDir,
                                         const std::filesystem::path& atsDir) noexcept {
  catalogRtsDir_ = rtsDir;
  catalogAtsDir_ = atsDir;
  catalog_.clear();
  std::size_t total = 0;
  total += catalog_.scan(rtsDir, data::SequenceType::RTS);
  total += catalog_.scan(atsDir, data::SequenceType::ATS);

  auto* log = componentLog();
  if (log != nullptr) {
    log->info(label(), fmt::format("Catalog scanned: {} RTS, {} ATS ({} total)",
                                   catalog_.rtsCount(), catalog_.atsCount(), total));
  }
  return total;
}

std::uint8_t ActionComponent::startRtsById(std::uint16_t sequenceId) noexcept {
  const auto* entry = catalog_.findById(sequenceId);
  if (entry == nullptr || !entry->binaryLoaded) {
    return 0xFF;
  }

  // Check blocking: is any running sequence in this entry's block list?
  for (std::uint8_t b = 0; b < entry->blockCount; ++b) {
    for (std::size_t s = 0; s < data::SEQUENCE_TABLE_SIZE; ++s) {
      if (data::isRunning(iface_.sequences[s]) &&
          iface_.sequences[s].sequenceId == entry->blocks[b]) {
        return 0xFF; // Blocked by a running sequence
      }
    }
  }

  // Mutual exclusion: stop any running sequence in the same exclusion group.
  if (entry->exclusionGroup != 0) {
    for (std::size_t s = 0; s < data::SEQUENCE_TABLE_SIZE; ++s) {
      if (!data::isRunning(iface_.sequences[s])) {
        continue;
      }
      const auto* running = catalog_.findById(iface_.sequences[s].sequenceId);
      if (running != nullptr && running->exclusionGroup == entry->exclusionGroup) {
        auto* log = componentLog();
        if (log != nullptr) {
          log->info(label(), fmt::format("Exclusion group {}: stopping RTS {} (slot {}) for RTS {}",
                                         entry->exclusionGroup, iface_.sequences[s].sequenceId, s,
                                         sequenceId));
        }
        data::abortSequence(iface_.sequences[s]);
        // Dispatch abort event immediately (slot may be reused by new sequence)
        if (iface_.sequences[s].abortEventPending) {
          iface_.sequences[s].abortEventPending = false;
          const std::uint16_t ABORT_EVT = iface_.sequences[s].abortEventId;
          data::dispatchEvents(iface_, &ABORT_EVT, 1);
          ++iface_.stats.abortEventsDispatched;
        }
        data::resetSequence(iface_.sequences[s]);
        ++iface_.stats.exclusionStops;
      }
    }
  }

  // Find a free execution slot (prefer RTS slots: first half of table)
  std::uint8_t freeSlot = 0xFF;
  std::uint8_t lowestPrioritySlot = 0xFF;
  std::uint8_t lowestPriority = 255;

  for (std::uint8_t s = 0; s < data::Config::RTS_SLOT_COUNT; ++s) {
    if (!data::isRunning(iface_.sequences[s])) {
      freeSlot = s;
      break;
    }
    // Track lowest-priority running sequence for preemption
    // (priority stored in catalog, look up by sequenceId)
    const auto* running = catalog_.findById(iface_.sequences[s].sequenceId);
    const std::uint8_t PRI = (running != nullptr) ? running->priority : 0;
    if (PRI < lowestPriority) {
      lowestPriority = PRI;
      lowestPrioritySlot = s;
    }
  }

  // If no free slot, try preemption
  if (freeSlot == 0xFF) {
    if (entry->priority > lowestPriority && lowestPrioritySlot < data::Config::RTS_SLOT_COUNT) {
      // Preempt lower-priority sequence. Abort sets abortEventPending, which
      // dispatchAbortEvents will pick up next processCycle. But we must
      // dispatch NOW because deserializeStandaloneSequence will overwrite the
      // slot (clearing the pending flag). Collect and dispatch immediately.
      data::abortSequence(iface_.sequences[lowestPrioritySlot]);
      if (iface_.sequences[lowestPrioritySlot].abortEventPending) {
        iface_.sequences[lowestPrioritySlot].abortEventPending = false;
        const std::uint16_t ABORT_EVT = iface_.sequences[lowestPrioritySlot].abortEventId;
        data::dispatchEvents(iface_, &ABORT_EVT, 1);
        ++iface_.stats.abortEventsDispatched;
      }
      data::resetSequence(iface_.sequences[lowestPrioritySlot]);
      freeSlot = lowestPrioritySlot;

      auto* log = componentLog();
      if (log != nullptr) {
        log->warning(label(), 0,
                     fmt::format("RTS {} preempted slot {} (priority {} > {})", sequenceId,
                                 freeSlot, entry->priority, lowestPriority));
      }
    } else {
      return 0xFF; // No slot available and can't preempt
    }
  }

  // Deserialize cached binary into execution slot
  StandaloneSequenceTprm tprm{};
  const std::size_t COPY_LEN =
      (entry->binary.size() <= sizeof(tprm)) ? entry->binary.size() : sizeof(tprm);
  std::memcpy(&tprm, entry->binary.data(), COPY_LEN);

  deserializeStandaloneSequence(freeSlot, tprm, data::SequenceType::RTS);

  // Copy catalog metadata to execution slot
  iface_.sequences[freeSlot].abortEventId = entry->abortEventId;

  // Start immediately
  const std::uint64_t START_REF = static_cast<std::uint64_t>(iface_.stats.totalCycles);
  data::startSequence(iface_.sequences[freeSlot], START_REF);

  auto* log = componentLog();
  if (log != nullptr) {
    log->info(label(), fmt::format("RTS started: id={} slot={} steps={} priority={}", sequenceId,
                                   freeSlot, entry->stepCount, entry->priority));
  }

  return freeSlot;
}

bool ActionComponent::stopRtsById(std::uint16_t sequenceId) noexcept {
  bool found = false;
  for (std::size_t s = 0; s < data::SEQUENCE_TABLE_SIZE; ++s) {
    if (data::isRunning(iface_.sequences[s]) && iface_.sequences[s].sequenceId == sequenceId) {
      data::abortSequence(iface_.sequences[s]);
      data::resetSequence(iface_.sequences[s]);

      auto* log = componentLog();
      if (log != nullptr) {
        log->info(label(), fmt::format("RTS stopped: id={} slot={}", sequenceId, s));
      }
      found = true;
    }
  }
  return found;
}

std::uint8_t ActionComponent::loadAtsFromCatalog() noexcept {
  std::uint8_t loaded = 0;
  const std::uint8_t ATS_START = static_cast<std::uint8_t>(data::Config::RTS_SLOT_COUNT);
  const std::uint8_t ATS_END = static_cast<std::uint8_t>(data::SEQUENCE_TABLE_SIZE);
  std::uint8_t nextSlot = ATS_START;

  catalog_.forEach([&](const data::CatalogEntry& entry) {
    if (nextSlot >= ATS_END) {
      return;
    }
    if (entry.type != data::SequenceType::ATS || !entry.binaryLoaded) {
      return;
    }

    // Deserialize cached binary into ATS execution slot
    StandaloneSequenceTprm tprm{};
    const std::size_t COPY_LEN =
        (entry.binary.size() <= sizeof(tprm)) ? entry.binary.size() : sizeof(tprm);
    std::memcpy(&tprm, entry.binary.data(), COPY_LEN);

    deserializeStandaloneSequence(nextSlot, tprm, data::SequenceType::ATS);

    // Copy catalog metadata to execution slot
    iface_.sequences[nextSlot].abortEventId = entry.abortEventId;

    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(), fmt::format("ATS loaded from catalog: id={} slot={} steps={}",
                                     entry.sequenceId, nextSlot, entry.stepCount));
    }

    ++loaded;
    ++nextSlot;
  });

  return loaded;
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
    // For START_RTS, gotoStep maps to chainTargetId
    stepDst.chainTargetId = static_cast<std::uint16_t>(SRC.gotoStep);
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

  // ActionComponent opcodes
  constexpr std::uint16_t GET_STATS = 0x0100;
  constexpr std::uint16_t LOAD_RTS = 0x0500;
  constexpr std::uint16_t START_RTS = 0x0501;
  constexpr std::uint16_t STOP_RTS = 0x0502;
  constexpr std::uint16_t LOAD_ATS = 0x0503;
  constexpr std::uint16_t START_ATS = 0x0504;
  constexpr std::uint16_t STOP_ATS = 0x0505;

  switch (opcode) {
  case GET_STATS: {
    ActionHealthTlm tlm{};
    const auto& S = iface_.stats;
    tlm.totalCycles = S.totalCycles;
    tlm.watchpointsFired = S.watchpointsFired;
    tlm.groupsFired = S.groupsFired;
    tlm.actionsApplied = S.actionsApplied;
    tlm.commandsRouted = S.commandsRouted;
    tlm.armControlsApplied = S.armControlsApplied;
    tlm.sequenceSteps = S.sequenceSteps;
    tlm.sequenceTimeouts = S.sequenceTimeouts;
    tlm.sequenceRetries = S.sequenceRetries;
    tlm.sequenceAborts = S.sequenceAborts;
    tlm.notificationsInvoked = S.notificationsInvoked;
    tlm.resolveFailures = S.resolveFailures;
    tlm.rtsLoaded = S.rtsLoaded;
    tlm.atsLoaded = S.atsLoaded;
    tlm.abortEventsDispatched = S.abortEventsDispatched;
    tlm.exclusionStops = S.exclusionStops;
    response.resize(sizeof(tlm));
    std::memcpy(response.data(), &tlm, sizeof(tlm));
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

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

  // ID-based sequence operations (catalog lookup, no slot index needed)
  case 0x0510: { // START_RTS_BY_ID: payload = [sequenceId_u16]
    if (payload.size() < 2) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    std::uint16_t seqId = 0;
    std::memcpy(&seqId, payload.data(), 2);
    const std::uint8_t SLOT = startRtsById(seqId);
    if (SLOT == 0xFF) {
      return static_cast<std::uint8_t>(CommandResult::LOAD_FAILED);
    }
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case 0x0511: { // STOP_RTS_BY_ID: payload = [sequenceId_u16]
    if (payload.size() < 2) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    std::uint16_t seqId = 0;
    std::memcpy(&seqId, payload.data(), 2);
    if (!stopRtsById(seqId)) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT);
    }
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case 0x0512: { // SET_PRIORITY: payload = [sequenceId_u16, priority_u8]
    if (payload.size() < 3) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    std::uint16_t seqId = 0;
    std::memcpy(&seqId, payload.data(), 2);
    auto* entry = catalog_.findByIdMut(seqId);
    if (entry == nullptr) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT);
    }
    entry->priority = payload[2];
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case 0x0513: { // SET_BLOCKING: payload = [sequenceId_u16, blockCount_u8, blockId0_u16, ...]
    if (payload.size() < 3) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    std::uint16_t seqId = 0;
    std::memcpy(&seqId, payload.data(), 2);
    auto* entry = catalog_.findByIdMut(seqId);
    if (entry == nullptr) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT);
    }
    entry->blockCount = payload[2];
    if (entry->blockCount > data::CATALOG_MAX_BLOCKS) {
      entry->blockCount = static_cast<std::uint8_t>(data::CATALOG_MAX_BLOCKS);
    }
    for (std::uint8_t i = 0; i < entry->blockCount && (3 + (i + 1) * 2) <= payload.size(); ++i) {
      std::memcpy(&entry->blocks[i], payload.data() + 3 + i * 2, 2);
    }
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case 0x0514: { // SET_ABORT_EVENT: payload = [sequenceId_u16, abortEventId_u16]
    if (payload.size() < 4) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    std::uint16_t seqId = 0;
    std::uint16_t abortEvt = 0;
    std::memcpy(&seqId, payload.data(), 2);
    std::memcpy(&abortEvt, payload.data() + 2, 2);
    auto* entry = catalog_.findByIdMut(seqId);
    if (entry == nullptr) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT);
    }
    entry->abortEventId = abortEvt;
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case 0x0515: { // SET_EXCLUSION_GROUP: payload = [sequenceId_u16, group_u8]
    if (payload.size() < 3) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    std::uint16_t seqId = 0;
    std::memcpy(&seqId, payload.data(), 2);
    auto* entry = catalog_.findByIdMut(seqId);
    if (entry == nullptr) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT);
    }
    entry->exclusionGroup = payload[2];
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case 0x0506: { // ABORT_ALL_RTS: no payload -- abort all running RTS sequences
    std::uint8_t aborted = 0;
    for (std::uint8_t s = 0; s < data::Config::RTS_SLOT_COUNT; ++s) {
      if (data::isRunning(iface_.sequences[s])) {
        data::abortSequence(iface_.sequences[s]);
        data::resetSequence(iface_.sequences[s]);
        ++aborted;
      }
    }
    auto* log = componentLog();
    if (log != nullptr && aborted > 0) {
      log->info(label(), fmt::format("ABORT_ALL_RTS: {} sequences aborted", aborted));
    }
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case 0x0520: { // RESCAN_CATALOG: no payload
    if (isRegistered()) {
      scanCatalog(catalogRtsDir_, catalogAtsDir_);
    }
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case 0x0521: { // GET_CATALOG: no payload -- return all catalog entries as packed telemetry
    // Wire format per entry: [sequenceId_u16, eventId_u16, type_u8, stepCount_u8,
    //                         priority_u8, armed_u8, abortEventId_u16, exclusionGroup_u8,
    //                         blockCount_u8] = 12 bytes per entry
    constexpr std::size_t ENTRY_SIZE = 12;
    const std::size_t COUNT = catalog_.size();
    response.resize(2 + COUNT * ENTRY_SIZE);
    const std::uint16_t ENTRY_COUNT = static_cast<std::uint16_t>(COUNT);
    std::memcpy(response.data(), &ENTRY_COUNT, 2);
    std::size_t offset = 2;
    catalog_.forEach([&](const data::CatalogEntry& e) {
      if (offset + ENTRY_SIZE > response.size()) {
        return;
      }
      std::memcpy(response.data() + offset, &e.sequenceId, 2);
      std::memcpy(response.data() + offset + 2, &e.eventId, 2);
      response[offset + 4] = static_cast<std::uint8_t>(e.type);
      response[offset + 5] = e.stepCount;
      response[offset + 6] = e.priority;
      response[offset + 7] = e.armed ? 1 : 0;
      std::memcpy(response.data() + offset + 8, &e.abortEventId, 2);
      response[offset + 10] = e.exclusionGroup;
      response[offset + 11] = e.blockCount;
      offset += ENTRY_SIZE;
    });
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case 0x0522: { // GET_STATUS: payload = [sequenceId_u16] -- return execution status
    if (payload.size() < 2) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    std::uint16_t seqId = 0;
    std::memcpy(&seqId, payload.data(), 2);

    // Search execution slots for this sequence ID
    // Wire format: [sequenceId_u16, status_u8, currentStep_u8, stepCount_u8,
    //               runCount_u32, slot_u8, type_u8] = 12 bytes
    for (std::size_t s = 0; s < data::SEQUENCE_TABLE_SIZE; ++s) {
      const auto& seq = iface_.sequences[s];
      if (seq.sequenceId == seqId && seq.stepCount > 0) {
        response.resize(12);
        std::memcpy(response.data(), &seqId, 2);
        response[2] = static_cast<std::uint8_t>(seq.status);
        response[3] = seq.currentStep;
        response[4] = seq.stepCount;
        std::memcpy(response.data() + 5, &seq.runCount, 4);
        response[9] = static_cast<std::uint8_t>(s);
        response[10] = static_cast<std::uint8_t>(seq.type);
        response[11] = seq.armed ? 1 : 0;
        return static_cast<std::uint8_t>(CommandResult::SUCCESS);
      }
    }

    // Not in execution slots -- check catalog for metadata
    const auto* entry = catalog_.findById(seqId);
    if (entry == nullptr) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT);
    }
    response.resize(12);
    std::memcpy(response.data(), &seqId, 2);
    response[2] = static_cast<std::uint8_t>(data::SequenceStatus::IDLE);
    response[3] = 0;
    response[4] = entry->stepCount;
    std::uint32_t zero = 0;
    std::memcpy(response.data() + 5, &zero, 4);
    response[9] = 0xFF; // No slot assigned
    response[10] = static_cast<std::uint8_t>(entry->type);
    response[11] = entry->armed ? 1 : 0;
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  // Resource catalog activate/deactivate commands
  case 0x0530: { // ACTIVATE_WP: payload = [watchpointId_u16]
    if (payload.size() < 2) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    std::uint16_t wpId = 0;
    std::memcpy(&wpId, payload.data(), 2);
    const auto* def = wpCatalog_.findById(wpId);
    if (def == nullptr) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT);
    }
    const std::uint8_t SLOT =
        data::activateWatchpoint(*def, iface_.watchpoints.data(), data::WATCHPOINT_TABLE_SIZE);
    if (SLOT == 0xFF) {
      return static_cast<std::uint8_t>(CommandResult::EXEC_FAILED);
    }
    data::rebuildWatchpointIndex(iface_);
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case 0x0531: { // DEACTIVATE_WP: payload = [watchpointId_u16]
    if (payload.size() < 2) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    std::uint16_t wpId = 0;
    std::memcpy(&wpId, payload.data(), 2);
    if (!data::deactivateWatchpoint(wpId, iface_.watchpoints.data(), data::WATCHPOINT_TABLE_SIZE)) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT);
    }
    data::rebuildWatchpointIndex(iface_);
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case 0x0532: { // ACTIVATE_GROUP: payload = [groupId_u16]
    if (payload.size() < 2) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    std::uint16_t grpId = 0;
    std::memcpy(&grpId, payload.data(), 2);
    const auto* def = grpCatalog_.findById(grpId);
    if (def == nullptr) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT);
    }
    const std::uint8_t SLOT =
        data::activateGroup(*def, iface_.groups.data(), data::WATCHPOINT_GROUP_TABLE_SIZE);
    if (SLOT == 0xFF) {
      return static_cast<std::uint8_t>(CommandResult::EXEC_FAILED);
    }
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case 0x0533: { // DEACTIVATE_GROUP: payload = [groupId_u16]
    if (payload.size() < 2) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    std::uint16_t grpId = 0;
    std::memcpy(&grpId, payload.data(), 2);
    if (!data::deactivateGroup(grpId, iface_.groups.data(), data::WATCHPOINT_GROUP_TABLE_SIZE)) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT);
    }
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case 0x0534: { // ACTIVATE_NOTIFICATION: payload = [notificationId_u16]
    if (payload.size() < 2) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    std::uint16_t noteId = 0;
    std::memcpy(&noteId, payload.data(), 2);
    const auto* def = noteCatalog_.findById(noteId);
    if (def == nullptr) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT);
    }
    const std::uint8_t SLOT = data::activateNotification(*def, iface_.notifications.data(),
                                                         data::EVENT_NOTIFICATION_TABLE_SIZE);
    if (SLOT == 0xFF) {
      return static_cast<std::uint8_t>(CommandResult::EXEC_FAILED);
    }
    data::rebuildEventIndex(iface_);
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case 0x0535: { // DEACTIVATE_NOTIFICATION: payload = [notificationId_u16]
    if (payload.size() < 2) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    std::uint16_t noteId = 0;
    std::memcpy(&noteId, payload.data(), 2);
    if (!data::deactivateNotification(noteId, iface_.notifications.data(),
                                      data::EVENT_NOTIFICATION_TABLE_SIZE)) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT);
    }
    data::rebuildEventIndex(iface_);
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
