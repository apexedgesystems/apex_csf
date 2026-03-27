#ifndef APEX_SYSTEM_CORE_ACTION_ENGINE_TPRM_HPP
#define APEX_SYSTEM_CORE_ACTION_ENGINE_TPRM_HPP
/**
 * @file ActionEngineTprm.hpp
 * @brief Binary TPRM structures for action engine configuration.
 *
 * Two TPRM formats:
 *
 * 1. ActionEngineTprm (monolithic, loaded at boot):
 *    Flat packed struct loaded via hex2cpp(). Contains watchpoints, groups,
 *    inline sequences (4 x 8 steps, basic format), notifications, and
 *    timed actions. Backward-compatible with existing TOML/binary files.
 *
 * 2. StandaloneSequenceTprm (per-file, loaded on demand):
 *    Expanded sequence format with full control flow (timeout, wait
 *    condition, retry, branching). Loaded into individual sequence
 *    slots via loadRts()/loadAts(). Stored in rts/ and ats/ filesystem
 *    directories.
 *
 * This struct defines the on-disk binary formats. TOML source files are
 * converted to these formats by cfg2bin.
 */

#include <cstdint>
#include <type_traits>

namespace system_core {
namespace action {

/* ----------------------------- Inline TPRM Structs ----------------------------- */

/**
 * @struct WatchpointTprm
 * @brief Binary representation of a single watchpoint.
 */
struct WatchpointTprm {
  std::uint32_t targetFullUid;    ///< Component fullUid.
  std::uint8_t targetCategory;    ///< DataCategory enum value.
  std::uint16_t targetByteOffset; ///< Byte offset within data block.
  std::uint8_t targetByteLen;     ///< Byte length.
  std::uint8_t predicate;         ///< WatchPredicate enum value.
  std::uint8_t dataType;          ///< WatchDataType enum value.
  std::uint8_t threshold[8];      ///< Raw threshold bytes.
  std::uint16_t eventId;          ///< Event to fire.
  std::uint16_t minFireCount;     ///< Debounce fire count.
  std::uint8_t armed;             ///< 1 = armed.
  std::uint8_t reserved[3];       ///< Padding to 24 bytes.
} __attribute__((packed));

static_assert(sizeof(WatchpointTprm) == 26, "WatchpointTprm must be 26 bytes");

/**
 * @struct GroupTprm
 * @brief Binary representation of a watchpoint group.
 */
struct GroupTprm {
  std::uint8_t refs[4];       ///< Watchpoint indices.
  std::uint8_t refCount;      ///< Number of valid indices.
  std::uint8_t logic;         ///< GroupLogic enum value.
  std::uint16_t eventId;      ///< Event to fire.
  std::uint16_t minFireCount; ///< Debounce fire count.
  std::uint8_t armed;         ///< 1 = armed.
  std::uint8_t reserved;      ///< Padding to 12 bytes.
} __attribute__((packed));

static_assert(sizeof(GroupTprm) == 12, "GroupTprm must be 12 bytes");

/**
 * @struct SequenceStepTprm
 * @brief Binary representation of a single sequence step (inline format).
 *
 * Basic format for sequences embedded in ActionEngineTprm.
 * For the expanded format with timeout/branching, see StandaloneStepTprm.
 */
struct SequenceStepTprm {
  std::uint32_t targetFullUid;    ///< Target component.
  std::uint8_t targetCategory;    ///< DataCategory enum value.
  std::uint16_t targetByteOffset; ///< Byte offset.
  std::uint8_t targetByteLen;     ///< Byte length.
  std::uint8_t actionType;        ///< ActionType enum value.
  std::uint8_t andMask[8];        ///< AND mask bytes.
  std::uint8_t xorMask[8];        ///< XOR mask bytes.
  std::uint32_t delayCycles;      ///< Delay before this step (from previous).
  std::uint32_t duration;         ///< Cycles to hold (0 = one-shot).
  std::uint8_t armTarget;         ///< ArmControlTarget enum (for ARM_CONTROL).
  std::uint8_t armIndex;          ///< Table index for ARM_CONTROL.
  std::uint8_t armState;          ///< 1 = arm, 0 = disarm.
  std::uint8_t reserved;          ///< Padding to 36 bytes.
} __attribute__((packed));

static_assert(sizeof(SequenceStepTprm) == 37, "SequenceStepTprm must be 37 bytes");

/**
 * @struct SequenceTprm
 * @brief Binary representation of a sequence (inline format).
 */
struct SequenceTprm {
  std::uint16_t eventId;     ///< Trigger event.
  std::uint8_t stepCount;    ///< Number of valid steps.
  std::uint8_t repeatCount;  ///< Repeat count (0 = run once).
  std::uint8_t type;         ///< SequenceType enum value.
  std::uint8_t armed;        ///< 1 = armed.
  std::uint8_t reserved[2];  ///< Padding to 8 bytes.
  SequenceStepTprm steps[8]; ///< Step table (8 inline steps).
} __attribute__((packed));

static_assert(sizeof(SequenceTprm) == 8 + 8 * 37, "SequenceTprm size mismatch");

/**
 * @struct NotificationTprm
 * @brief Binary representation of an event notification.
 */
struct NotificationTprm {
  std::uint16_t eventId;    ///< Event to listen for.
  char logLabel[16];        ///< Log label (e.g., "ACTION").
  char logMessage[48];      ///< Log message text.
  std::uint8_t logSeverity; ///< LogSeverity enum value.
  std::uint8_t armed;       ///< 1 = armed.
} __attribute__((packed));

static_assert(sizeof(NotificationTprm) == 68, "NotificationTprm must be 68 bytes");

/**
 * @struct TimedActionTprm
 * @brief Binary representation of a timed action.
 */
struct TimedActionTprm {
  std::uint32_t targetFullUid;    ///< Target component.
  std::uint8_t targetCategory;    ///< DataCategory enum value.
  std::uint16_t targetByteOffset; ///< Byte offset.
  std::uint8_t targetByteLen;     ///< Byte length.
  std::uint8_t actionType;        ///< ActionType enum value.
  std::uint8_t trigger;           ///< ActionTrigger enum value.
  std::uint32_t triggerParam;     ///< Cycle count for AT_CYCLE, etc.
  std::uint32_t duration;         ///< Cycles to hold.
  std::uint8_t andMask[8];        ///< AND mask bytes.
  std::uint8_t xorMask[8];        ///< XOR mask bytes.
  std::uint8_t armTarget;         ///< ArmControlTarget enum (for ARM_CONTROL).
  std::uint8_t armIndex;          ///< Table index for ARM_CONTROL.
  std::uint8_t armState;          ///< 1 = arm, 0 = disarm.
  std::uint8_t reserved;          ///< Padding to 40 bytes.
} __attribute__((packed));

static_assert(sizeof(TimedActionTprm) == 38, "TimedActionTprm must be 38 bytes");

/* ----------------------------- ActionEngineTprm ----------------------------- */

/**
 * @struct ActionEngineTprm
 * @brief Complete binary TPRM for the action engine (monolithic boot config).
 *
 * Loaded by ActionComponent::loadTprm() and deserialized into the live
 * ActionInterface tables. Backward-compatible: inline sequences use the
 * basic SequenceStepTprm format (no timeout/branching fields).
 *
 * For expanded sequences, use StandaloneSequenceTprm loaded via loadRts()/loadAts().
 */
struct ActionEngineTprm {
  WatchpointTprm watchpoints[8];     ///< 8 x 26 = 208 bytes.
  GroupTprm groups[4];               ///< 4 x 12 = 48 bytes.
  SequenceTprm sequences[4];         ///< 4 x 304 = 1216 bytes.
  NotificationTprm notifications[8]; ///< 8 x 68 = 544 bytes.
  TimedActionTprm actions[16];       ///< 16 x 38 = 608 bytes.
} __attribute__((packed));

// Total: 208 + 48 + 1216 + 544 + 608 = 2624 bytes
static_assert(sizeof(ActionEngineTprm) == 2624, "ActionEngineTprm size mismatch");

// Required for hex2cpp loading.
static_assert(std::is_trivially_copyable_v<ActionEngineTprm>,
              "ActionEngineTprm must be trivially copyable");

/* ----------------------------- Standalone RTS/ATS Structs ----------------------------- */

/**
 * @struct StandaloneWaitConditionTprm
 * @brief Binary representation of a step wait condition.
 */
struct StandaloneWaitConditionTprm {
  std::uint32_t targetFullUid;    ///< Component fullUid.
  std::uint8_t targetCategory;    ///< DataCategory enum value.
  std::uint16_t targetByteOffset; ///< Byte offset.
  std::uint8_t targetByteLen;     ///< Byte length.
  std::uint8_t predicate;         ///< WatchPredicate enum value.
  std::uint8_t dataType;          ///< WatchDataType enum value.
  std::uint8_t threshold[8];      ///< Raw threshold bytes.
  std::uint8_t enabled;           ///< 1 = condition active.
  std::uint8_t reserved;          ///< Padding.
} __attribute__((packed));

static_assert(sizeof(StandaloneWaitConditionTprm) == 20,
              "StandaloneWaitConditionTprm must be 20 bytes");

/**
 * @struct StandaloneStepTprm
 * @brief Expanded step format for standalone RTS/ATS files.
 *
 * Includes all control flow fields: timeout, branching, retry, wait
 * condition, and command payload.
 */
struct StandaloneStepTprm {
  /* ---- Action target ---- */
  std::uint32_t targetFullUid;    ///< Target component.
  std::uint8_t targetCategory;    ///< DataCategory enum value.
  std::uint16_t targetByteOffset; ///< Byte offset.
  std::uint8_t targetByteLen;     ///< Byte length.
  std::uint8_t actionType;        ///< ActionType enum value.

  /* ---- DATA_WRITE fields ---- */
  std::uint8_t andMask[8]; ///< AND mask bytes.
  std::uint8_t xorMask[8]; ///< XOR mask bytes.
  std::uint32_t duration;  ///< Cycles to hold (0 = one-shot).

  /* ---- ARM_CONTROL fields ---- */
  std::uint8_t armTarget; ///< ArmControlTarget enum.
  std::uint8_t armIndex;  ///< Table index.
  std::uint8_t armState;  ///< 1 = arm, 0 = disarm.

  /* ---- COMMAND fields ---- */
  std::uint16_t commandOpcode;     ///< Command opcode.
  std::uint8_t commandPayloadLen;  ///< Payload length.
  std::uint8_t commandPayload[16]; ///< Command payload bytes.

  /* ---- Timing ---- */
  std::uint32_t delayCycles;   ///< Delay (RTS: relative, ATS: absolute offset).
  std::uint32_t timeoutCycles; ///< Max wait (0 = no timeout).

  /* ---- Control flow ---- */
  std::uint8_t onTimeout;  ///< StepTimeoutPolicy enum.
  std::uint8_t onComplete; ///< StepCompletionAction enum.
  std::uint8_t gotoStep;   ///< Target for GOTO_STEP or START_RTS slot.
  std::uint8_t retryMax;   ///< Max retries on timeout.

  /* ---- Wait condition ---- */
  StandaloneWaitConditionTprm waitCondition; ///< Optional hold condition.

  std::uint8_t reserved; ///< Padding.
} __attribute__((packed));

static_assert(sizeof(StandaloneStepTprm) == 84, "StandaloneStepTprm must be 84 bytes");

/**
 * @struct StandaloneSequenceTprm
 * @brief Expanded sequence format for standalone RTS/ATS files.
 *
 * Loaded by ActionComponent::loadRts()/loadAts() into individual
 * sequence table slots. Stored as raw hex2cpp binary in rts/ or ats/
 * filesystem directories.
 */
struct StandaloneSequenceTprm {
  std::uint16_t sequenceId;     ///< Unique ID for logging/telemetry.
  std::uint16_t eventId;        ///< Trigger event (0 = manual start only).
  std::uint8_t stepCount;       ///< Number of valid steps (0-16).
  std::uint8_t repeatCount;     ///< Repeat count (0 = once, 0xFF = forever).
  std::uint8_t type;            ///< SequenceType enum (0=RTS, 1=ATS).
  std::uint8_t armed;           ///< 1 = armed on load.
  StandaloneStepTprm steps[16]; ///< Step table (SEQUENCE_MAX_STEPS).
} __attribute__((packed));

// Header: 8 bytes. Steps: 16 * 84 = 1344 bytes. Total: 1352 bytes.
static_assert(sizeof(StandaloneSequenceTprm) == 1352, "StandaloneSequenceTprm size mismatch");

static_assert(std::is_trivially_copyable_v<StandaloneSequenceTprm>,
              "StandaloneSequenceTprm must be trivially copyable");

} // namespace action
} // namespace system_core

#endif // APEX_SYSTEM_CORE_ACTION_ENGINE_TPRM_HPP
