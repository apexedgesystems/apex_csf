/**
 * @file DataTransform.cpp
 * @brief DataTransform support component implementation.
 */

#include "src/system/core/support/data_transform/inc/DataTransform.hpp"

#include "src/system/core/infrastructure/system_component/posix/inc/IInternalBus.hpp"
#include "src/utilities/helpers/inc/Files.hpp"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <unistd.h>
#include <vector>

namespace system_core {
namespace support {

/* ----------------------------- Lifecycle ----------------------------- */

std::uint8_t DataTransform::doInit() noexcept {
  if (!resolver_) {
    return static_cast<std::uint8_t>(Status::ERROR_PARAM);
  }

  registerTask<DataTransform, &DataTransform::telemetry>(
      static_cast<std::uint8_t>(TaskUid::TELEMETRY), this, "telemetry");

  registerData(data::DataCategory::STATE, "stats", &stats_, sizeof(TransformStats));

  return static_cast<std::uint8_t>(Status::SUCCESS);
}

void DataTransform::doReset() noexcept {
  for (auto& entry : entries_) {
    entry.armed = false;
    entry.proxy.clear();
  }
  stats_ = {};
}

/* ----------------------------- TPRM Loading ----------------------------- */

bool DataTransform::loadTprm(const std::filesystem::path& tprmDir) noexcept {
  if (!isRegistered()) {
    return false;
  }

  char filename[32];
  std::snprintf(filename, sizeof(filename), "%06x.tprm", fullUid());
  const std::filesystem::path TPRM_PATH = tprmDir / filename;

  if (!std::filesystem::exists(TPRM_PATH)) {
    setConfigured(true);
    return false;
  }

  // Load the fault campaign TPRM
  std::string error;
  if (apex::helpers::files::hex2cpp(TPRM_PATH.string(), campaign_, error)) {
    hasCampaign_ = (campaign_.entryCount > 0);
    setConfigured(true);

    auto* log = componentLog();
    if (log != nullptr) {
      char buf[96];
      std::snprintf(buf, sizeof(buf), "TPRM loaded: %u fault entries", campaign_.entryCount);
      log->info(label(), buf);
    }

    // Generate the ATS binary and write to ats/ directory
    if (hasCampaign_) {
      // Derive ats directory from tprm directory (sibling: {root}/tprm -> {root}/ats)
      const std::filesystem::path ATS_DIR = tprmDir / "../ats";
      std::filesystem::create_directories(ATS_DIR);

      atsPath_ = ATS_DIR / "002.ats";

      std::array<std::uint8_t, 1032> atsBinary{};
      if (buildFaultAts(campaign_, atsBinary)) {
        // Write raw binary to file
        int fd = ::open(atsPath_.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (fd >= 0) {
          [[maybe_unused]] auto written = ::write(fd, atsBinary.data(), atsBinary.size());
          ::close(fd);
          if (log != nullptr) {
            log->info(label(), "Fault campaign ATS generated: " + atsPath_.string());
          }
        }
      }
    }

    return true;
  }

  setConfigured(true);
  return false;
}

/* ----------------------------- ATS Generation ----------------------------- */

bool DataTransform::buildFaultAts(const FaultCampaignTprm& campaign,
                                  std::array<std::uint8_t, 1032>& outBinary) noexcept {
  // StandaloneSequenceTprm layout:
  //   Header (8 bytes): sequenceId(2), eventId(2), stepCount(1), repeatCount(1), type(1), armed(1)
  //   Steps (16 x 84 bytes): each step is a StandaloneStepTprm

  std::memset(outBinary.data(), 0, outBinary.size());

  const std::uint8_t COUNT = (campaign.entryCount > FAULT_CAMPAIGN_MAX_ENTRIES)
                                 ? static_cast<std::uint8_t>(FAULT_CAMPAIGN_MAX_ENTRIES)
                                 : campaign.entryCount;
  if (COUNT == 0) {
    return false;
  }

  // Each fault entry generates up to 5 steps:
  //   SET_TARGET, ARM_ENTRY, PUSH_*_MASK, APPLY_ENTRY, [DISARM if duration > 0]
  // One-shot (duration=0): 5 steps (SET_TARGET, ARM, PUSH, APPLY, DISARM)
  // Duration (duration>0): 4 steps at trigger + 1 step at trigger+duration (DISARM)
  // ATS timing: each step's delayCycles is absolute cycle offset from sequence start.
  // Max 16 steps total, so max ~3 faults with duration or ~3 one-shot faults.

  // DataTransform protocol constants (no header dependency on action engine)
  constexpr std::uint16_t OP_SET_TARGET = 0x0609;
  constexpr std::uint16_t OP_ARM_ENTRY = 0x0601;
  constexpr std::uint16_t OP_PUSH_ZERO = 0x0603;
  constexpr std::uint16_t OP_PUSH_HIGH = 0x0604;
  constexpr std::uint16_t OP_PUSH_FLIP = 0x0605;
  constexpr std::uint16_t OP_PUSH_CUSTOM = 0x0606;
  constexpr std::uint16_t OP_APPLY_ENTRY = 0x060A;
  constexpr std::uint16_t OP_DISARM_ENTRY = 0x0602;

  // Action engine fullUid (protocol constant, not a header dependency)
  // constexpr std::uint32_t ACTION_UID = 0x000500; -- not needed, targeting self

  // DataTransform fullUid (self)
  const std::uint32_t DT_UID = fullUid();

  // Build steps
  struct StepDef {
    std::uint32_t absCycle;
    std::uint16_t opcode;
    std::uint8_t payloadLen;
    std::uint8_t payload[16];
  };

  StepDef steps[16]{};
  std::uint8_t stepCount = 0;

  for (std::uint8_t i = 0; i < COUNT && stepCount < 14; ++i) {
    const auto& F = campaign.entries[i];
    const std::uint8_t ENTRY_IDX = i;
    // Convert cycle count to ATS offset. When a time provider is wired,
    // ATS delayCycles are microseconds. Convert if clock frequency is known.
    const std::uint32_t TRIGGER =
        (clockFrequencyHz_ > 0) ? F.triggerCycle * (1000000U / clockFrequencyHz_) : F.triggerCycle;

    // Step: SET_TARGET
    // Payload: [index, uid_le32, category, offset_le16, len]
    if (stepCount < 16) {
      auto& s = steps[stepCount++];
      s.absCycle = TRIGGER;
      s.opcode = OP_SET_TARGET;
      s.payloadLen = 9;
      s.payload[0] = ENTRY_IDX;
      std::memcpy(&s.payload[1], &F.targetFullUid, 4);
      s.payload[5] = F.targetCategory;
      std::memcpy(&s.payload[6], &F.targetByteOffset, 2);
      s.payload[8] = F.targetByteLen;
    }

    // Step: ARM_ENTRY
    if (stepCount < 16) {
      auto& s = steps[stepCount++];
      s.absCycle = TRIGGER;
      s.opcode = OP_ARM_ENTRY;
      s.payloadLen = 1;
      s.payload[0] = ENTRY_IDX;
    }

    // Step: PUSH_*_MASK
    if (stepCount < 16) {
      auto& s = steps[stepCount++];
      s.absCycle = TRIGGER;
      switch (static_cast<MaskType>(F.maskType)) {
      case MaskType::ZERO:
        s.opcode = OP_PUSH_ZERO;
        s.payloadLen = 4;
        s.payload[0] = ENTRY_IDX;
        s.payload[1] = 0; // offset_lo
        s.payload[2] = 0; // offset_hi
        s.payload[3] = F.targetByteLen;
        break;
      case MaskType::HIGH:
        s.opcode = OP_PUSH_HIGH;
        s.payloadLen = 4;
        s.payload[0] = ENTRY_IDX;
        s.payload[1] = 0;
        s.payload[2] = 0;
        s.payload[3] = F.targetByteLen;
        break;
      case MaskType::FLIP:
        s.opcode = OP_PUSH_FLIP;
        s.payloadLen = 4;
        s.payload[0] = ENTRY_IDX;
        s.payload[1] = 0;
        s.payload[2] = 0;
        s.payload[3] = F.targetByteLen;
        break;
      case MaskType::CUSTOM:
        s.opcode = OP_PUSH_CUSTOM;
        s.payloadLen = static_cast<std::uint8_t>(4 + 2 * F.targetByteLen);
        if (s.payloadLen > 16) {
          s.payloadLen = 16;
        }
        s.payload[0] = ENTRY_IDX;
        s.payload[1] = 0;
        s.payload[2] = 0;
        s.payload[3] = F.targetByteLen;
        std::memcpy(&s.payload[4], F.customAnd, F.targetByteLen);
        std::memcpy(&s.payload[4 + F.targetByteLen], F.customXor, F.targetByteLen);
        break;
      }
    }

    // Step: APPLY_ENTRY
    if (stepCount < 16) {
      auto& s = steps[stepCount++];
      s.absCycle = TRIGGER;
      s.opcode = OP_APPLY_ENTRY;
      s.payloadLen = 1;
      s.payload[0] = ENTRY_IDX;
    }

    // Step: DISARM_ENTRY (at trigger + duration, or immediately if one-shot)
    if (stepCount < 16) {
      auto& s = steps[stepCount++];
      const std::uint32_t DURATION_OFFSET = (clockFrequencyHz_ > 0 && F.durationCycles > 0)
                                                ? F.durationCycles * (1000000U / clockFrequencyHz_)
                                                : F.durationCycles;
      s.absCycle = (F.durationCycles > 0) ? (TRIGGER + DURATION_OFFSET) : TRIGGER;
      s.opcode = OP_DISARM_ENTRY;
      s.payloadLen = 1;
      s.payload[0] = ENTRY_IDX;
    }
  }

  // Sort steps by absCycle (simple insertion sort, max 16 entries)
  for (std::uint8_t i = 1; i < stepCount; ++i) {
    StepDef key = steps[i];
    std::int8_t j = static_cast<std::int8_t>(i) - 1;
    while (j >= 0 && steps[j].absCycle > key.absCycle) {
      steps[j + 1] = steps[j];
      --j;
    }
    steps[j + 1] = key;
  }

  // Write header (8 bytes)
  std::uint8_t* p = outBinary.data();
  const std::uint16_t SEQ_ID = 200; // Fault campaign sequence ID
  const std::uint16_t EVENT_ID = 0; // No event trigger (started via bus command)
  std::memcpy(p, &SEQ_ID, 2);
  p += 2;
  std::memcpy(p, &EVENT_ID, 2);
  p += 2;
  *p++ = stepCount;
  *p++ = 0; // repeatCount
  *p++ = 1; // type = ATS (absolute cycle offsets)
  *p++ = 1; // armed = 1

  // Write steps (16 x 64 bytes each)
  // StandaloneStepTprm layout (64 bytes):
  //   [0]  targetFullUid (4)
  //   [4]  targetCategory (1)
  //   [5]  targetByteOffset (2)
  //   [7]  targetByteLen (1)
  //   [8]  actionType (1)    -- COMMAND = 0
  //   [9]  armTarget (1)
  //   [10] armIndex (1)
  //   [11] armState (1)
  //   [12] commandOpcode (2)
  //   [14] commandPayloadLen (1)
  //   [15] commandPayload (16)
  //   [31] delayCycles (4)   -- ATS absolute cycle offset
  //   [35] timeoutCycles (4)
  //   [39] onTimeout (1)     -- SKIP = 1
  //   [40] onComplete (1)    -- NEXT = 0
  //   [41] gotoStep (1)
  //   [42] retryMax (1)
  //   [43] waitCondition (20) -- all zeros (disabled)
  //   [63] reserved (1)

  for (std::uint8_t i = 0; i < 16; ++i) {
    std::uint8_t step[64]{};

    if (i < stepCount) {
      const auto& S = steps[i];

      // targetFullUid = DataTransform's own fullUid (command target is self)
      std::memcpy(&step[0], &DT_UID, 4);
      // actionType = COMMAND (0)
      step[8] = 0;
      // commandOpcode
      std::memcpy(&step[12], &S.opcode, 2);
      // commandPayloadLen
      step[14] = S.payloadLen;
      // commandPayload
      std::memcpy(&step[15], S.payload, S.payloadLen);
      // delayCycles (ATS absolute offset)
      std::memcpy(&step[31], &S.absCycle, 4);
      // timeoutCycles = 50 (5 seconds at 10 Hz)
      const std::uint32_t TIMEOUT = 50;
      std::memcpy(&step[35], &TIMEOUT, 4);
      // onTimeout = SKIP (1)
      step[39] = 1;
    }

    std::memcpy(p, step, 64);
    p += 64;
  }

  return true;
}

/* ----------------------------- Bus Ready ----------------------------- */

void DataTransform::onBusReady() noexcept {
  if (!hasCampaign_) {
    return;
  }

  auto* bus = internalBus();
  if (bus == nullptr) {
    auto* log = componentLog();
    if (log != nullptr) {
      log->warning(label(), 0, "onBusReady: internal bus not available");
    }
    return;
  }

  // The ATS file was written during loadTprm() and auto-loaded by the executive.
  // Send START_ATS to begin the fault campaign immediately.
  constexpr std::uint32_t ACTION_FULLUID = 0x000500;
  constexpr std::uint16_t START_ATS_OPCODE = 0x0504;
  constexpr std::uint8_t ATS_SLOT = 2;

  std::array<std::uint8_t, 1> payload = {ATS_SLOT};
  const bool SENT =
      bus->postInternalCommand(fullUid(), ACTION_FULLUID, START_ATS_OPCODE,
                               apex::compat::rospan<std::uint8_t>{payload.data(), payload.size()});

  auto* log = componentLog();
  if (log != nullptr) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "START_ATS sent to action engine: slot=%u sent=%d", ATS_SLOT,
                  SENT ? 1 : 0);
    log->info(label(), buf);
  }
}

/* ----------------------------- Scheduled Tasks ----------------------------- */

std::uint8_t DataTransform::telemetry() noexcept {
  std::uint32_t armed = 0;
  for (const auto& entry : entries_) {
    if (entry.armed) {
      ++armed;
    }
  }
  stats_.entriesArmed = armed;

  auto* log = componentLog();
  if (log != nullptr) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "armed=%u applied=%u resolve_fail=%u apply_fail=%u", armed,
                  stats_.masksApplied, stats_.resolveFailures, stats_.applyFailures);
    log->info(label(), buf);
  }
  return 0;
}

/* ----------------------------- Private Helpers ----------------------------- */

bool DataTransform::applyEntry(std::uint8_t index) noexcept {
  if (index >= TRANSFORM_MAX_ENTRIES) {
    return false;
  }

  auto& entry = entries_[index];
  if (!entry.armed || entry.proxy.empty()) {
    return false;
  }

  // Resolve target to byte pointer
  auto resolved = resolver_(entry.target.fullUid, entry.target.category);
  if (resolved.data == nullptr) {
    ++stats_.resolveFailures;
    return false;
  }

  // Offset into the resolved block
  std::uint8_t* targetBytes = resolved.data + entry.target.byteOffset;
  const std::size_t AVAILABLE =
      (entry.target.byteOffset < resolved.size) ? (resolved.size - entry.target.byteOffset) : 0;

  if (AVAILABLE == 0) {
    ++stats_.resolveFailures;
    return false;
  }

  // Capture before-state for logging (up to 8 bytes)
  const std::uint8_t LOG_LEN =
      (entry.target.byteLen <= 8) ? entry.target.byteLen : static_cast<std::uint8_t>(8);
  std::uint8_t beforeBytes[8]{};
  std::memcpy(beforeBytes, targetBytes, LOG_LEN);

  // Apply the front mask
  auto status = entry.proxy.apply(targetBytes, AVAILABLE);
  if (status == data_proxy::ByteMaskStatus::SUCCESS) {
    ++stats_.masksApplied;

    auto* log = componentLog();
    if (log != nullptr) {
      // Format before/after hex for the affected bytes
      char before[24]{};
      char after[24]{};
      for (std::uint8_t b = 0; b < LOG_LEN; ++b) {
        std::snprintf(before + b * 2, 3, "%02X", beforeBytes[b]);
        std::snprintf(after + b * 2, 3, "%02X", targetBytes[b]);
      }

      char buf[160];
      std::snprintf(buf, sizeof(buf),
                    "APPLY entry=%u uid=0x%06X off=%u len=%u before=[%s] after=[%s] masks=%u",
                    index, entry.target.fullUid, entry.target.byteOffset, entry.target.byteLen,
                    before, after, stats_.masksApplied);
      log->info(label(), buf);
    }
    return true;
  }

  ++stats_.applyFailures;
  return false;
}

/* ----------------------------- Command Interface ----------------------------- */

std::uint8_t DataTransform::handleCommand(std::uint16_t opcode,
                                          apex::compat::rospan<std::uint8_t> payload,
                                          std::vector<std::uint8_t>& response) noexcept {
  const auto OP = static_cast<DataTransformOpcode>(opcode);

  switch (OP) {
  case DataTransformOpcode::GET_STATS: {
    DataTransformTlm tlm{};
    tlm.applyCycles = stats_.applyCycles;
    tlm.masksApplied = stats_.masksApplied;
    tlm.resolveFailures = stats_.resolveFailures;
    tlm.applyFailures = stats_.applyFailures;
    tlm.entriesArmed = stats_.entriesArmed;
    response.resize(sizeof(tlm));
    std::memcpy(response.data(), &tlm, sizeof(tlm));
    return static_cast<std::uint8_t>(Status::SUCCESS);
  }

  case DataTransformOpcode::ARM_ENTRY: {
    if (payload.size() < 1) {
      return static_cast<std::uint8_t>(Status::ERROR_PARAM);
    }
    const std::uint8_t INDEX = payload[0];
    if (INDEX >= TRANSFORM_MAX_ENTRIES) {
      return static_cast<std::uint8_t>(Status::ERROR_PARAM);
    }
    entries_[INDEX].armed = true;
    {
      auto* log = componentLog();
      if (log != nullptr) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "ARM entry=%u uid=0x%06X", INDEX,
                      entries_[INDEX].target.fullUid);
        log->info(label(), buf);
      }
    }
    return static_cast<std::uint8_t>(Status::SUCCESS);
  }

  case DataTransformOpcode::DISARM_ENTRY: {
    if (payload.size() < 1) {
      return static_cast<std::uint8_t>(Status::ERROR_PARAM);
    }
    const std::uint8_t INDEX = payload[0];
    if (INDEX >= TRANSFORM_MAX_ENTRIES) {
      return static_cast<std::uint8_t>(Status::ERROR_PARAM);
    }
    entries_[INDEX].armed = false;
    {
      auto* log = componentLog();
      if (log != nullptr) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "DISARM entry=%u", INDEX);
        log->info(label(), buf);
      }
    }
    return static_cast<std::uint8_t>(Status::SUCCESS);
  }

  case DataTransformOpcode::PUSH_ZERO_MASK: {
    if (payload.size() < 4) {
      return static_cast<std::uint8_t>(Status::ERROR_PARAM);
    }
    const std::uint8_t INDEX = payload[0];
    if (INDEX >= TRANSFORM_MAX_ENTRIES) {
      return static_cast<std::uint8_t>(Status::ERROR_PARAM);
    }
    const std::size_t OFFSET =
        static_cast<std::size_t>(payload[1]) | (static_cast<std::size_t>(payload[2]) << 8);
    const std::uint8_t LEN = payload[3];
    auto result = entries_[INDEX].proxy.pushZeroMask(OFFSET, LEN);
    return (result == data_proxy::ByteMaskStatus::SUCCESS)
               ? static_cast<std::uint8_t>(Status::SUCCESS)
               : static_cast<std::uint8_t>(Status::ERROR_PARAM);
  }

  case DataTransformOpcode::PUSH_HIGH_MASK: {
    if (payload.size() < 4) {
      return static_cast<std::uint8_t>(Status::ERROR_PARAM);
    }
    const std::uint8_t INDEX = payload[0];
    if (INDEX >= TRANSFORM_MAX_ENTRIES) {
      return static_cast<std::uint8_t>(Status::ERROR_PARAM);
    }
    const std::size_t OFFSET =
        static_cast<std::size_t>(payload[1]) | (static_cast<std::size_t>(payload[2]) << 8);
    const std::uint8_t LEN = payload[3];
    auto result = entries_[INDEX].proxy.pushHighMask(OFFSET, LEN);
    return (result == data_proxy::ByteMaskStatus::SUCCESS)
               ? static_cast<std::uint8_t>(Status::SUCCESS)
               : static_cast<std::uint8_t>(Status::ERROR_PARAM);
  }

  case DataTransformOpcode::PUSH_FLIP_MASK: {
    if (payload.size() < 4) {
      return static_cast<std::uint8_t>(Status::ERROR_PARAM);
    }
    const std::uint8_t INDEX = payload[0];
    if (INDEX >= TRANSFORM_MAX_ENTRIES) {
      return static_cast<std::uint8_t>(Status::ERROR_PARAM);
    }
    const std::size_t OFFSET =
        static_cast<std::size_t>(payload[1]) | (static_cast<std::size_t>(payload[2]) << 8);
    const std::uint8_t LEN = payload[3];
    auto result = entries_[INDEX].proxy.pushFlipMask(OFFSET, LEN);
    return (result == data_proxy::ByteMaskStatus::SUCCESS)
               ? static_cast<std::uint8_t>(Status::SUCCESS)
               : static_cast<std::uint8_t>(Status::ERROR_PARAM);
  }

  case DataTransformOpcode::PUSH_CUSTOM_MASK: {
    if (payload.size() < 4) {
      return static_cast<std::uint8_t>(Status::ERROR_PARAM);
    }
    const std::uint8_t INDEX = payload[0];
    if (INDEX >= TRANSFORM_MAX_ENTRIES) {
      return static_cast<std::uint8_t>(Status::ERROR_PARAM);
    }
    const std::size_t OFFSET =
        static_cast<std::size_t>(payload[1]) | (static_cast<std::size_t>(payload[2]) << 8);
    const std::uint8_t LEN = payload[3];
    if (payload.size() < static_cast<std::size_t>(4 + 2 * LEN)) {
      return static_cast<std::uint8_t>(Status::ERROR_PARAM);
    }
    const std::uint8_t* AND_PTR = payload.data() + 4;
    const std::uint8_t* XOR_PTR = payload.data() + 4 + LEN;
    auto result = entries_[INDEX].proxy.push(OFFSET, AND_PTR, XOR_PTR, LEN);
    return (result == data_proxy::ByteMaskStatus::SUCCESS)
               ? static_cast<std::uint8_t>(Status::SUCCESS)
               : static_cast<std::uint8_t>(Status::ERROR_PARAM);
  }

  case DataTransformOpcode::CLEAR_MASKS: {
    if (payload.size() < 1) {
      return static_cast<std::uint8_t>(Status::ERROR_PARAM);
    }
    const std::uint8_t INDEX = payload[0];
    if (INDEX >= TRANSFORM_MAX_ENTRIES) {
      return static_cast<std::uint8_t>(Status::ERROR_PARAM);
    }
    entries_[INDEX].proxy.clear();
    return static_cast<std::uint8_t>(Status::SUCCESS);
  }

  case DataTransformOpcode::CLEAR_ALL: {
    for (auto& entry : entries_) {
      entry.armed = false;
      entry.proxy.clear();
    }
    stats_.entriesArmed = 0;
    return static_cast<std::uint8_t>(Status::SUCCESS);
  }

  case DataTransformOpcode::SET_TARGET: {
    // Payload: [index, fullUid_le32, category, offset_le16, len]
    if (payload.size() < 9) {
      return static_cast<std::uint8_t>(Status::ERROR_PARAM);
    }
    const std::uint8_t INDEX = payload[0];
    if (INDEX >= TRANSFORM_MAX_ENTRIES) {
      return static_cast<std::uint8_t>(Status::ERROR_PARAM);
    }
    const std::uint32_t UID = static_cast<std::uint32_t>(payload[1]) |
                              (static_cast<std::uint32_t>(payload[2]) << 8) |
                              (static_cast<std::uint32_t>(payload[3]) << 16) |
                              (static_cast<std::uint32_t>(payload[4]) << 24);
    const auto CAT = static_cast<data::DataCategory>(payload[5]);
    const std::uint16_t OFF =
        static_cast<std::uint16_t>(payload[6]) | (static_cast<std::uint16_t>(payload[7]) << 8);
    const std::uint8_t LEN = payload[8];
    entries_[INDEX].target = {UID, CAT, OFF, LEN};
    {
      auto* log = componentLog();
      if (log != nullptr) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "SET_TARGET entry=%u uid=0x%06X cat=%u off=%u len=%u",
                      INDEX, UID, static_cast<unsigned>(CAT), OFF, LEN);
        log->info(label(), buf);
      }
    }
    return static_cast<std::uint8_t>(Status::SUCCESS);
  }

  case DataTransformOpcode::APPLY_ENTRY: {
    if (payload.size() < 1) {
      return static_cast<std::uint8_t>(Status::ERROR_PARAM);
    }
    const std::uint8_t INDEX = payload[0];
    ++stats_.applyCycles;
    bool ok = applyEntry(INDEX);
    return ok ? static_cast<std::uint8_t>(Status::SUCCESS)
              : static_cast<std::uint8_t>(Status::ERROR_PARAM);
  }

  case DataTransformOpcode::APPLY_ALL: {
    ++stats_.applyCycles;
    for (std::uint8_t i = 0; i < TRANSFORM_MAX_ENTRIES; ++i) {
      if (entries_[i].armed && !entries_[i].proxy.empty()) {
        applyEntry(i);
      }
    }
    return static_cast<std::uint8_t>(Status::SUCCESS);
  }

  default:
    return SupportComponentBase::handleCommand(opcode, payload, response);
  }
}

} // namespace support
} // namespace system_core
