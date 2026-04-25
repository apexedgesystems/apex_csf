#ifndef APEX_SYSTEM_CORE_ACTION_ACTION_TLM_HPP
#define APEX_SYSTEM_CORE_ACTION_ACTION_TLM_HPP
/**
 * @file ActionTlm.hpp
 * @brief Telemetry wire format for ActionComponent stats queries.
 *
 * Packed POD struct returned by GET_STATS (opcode 0x0100) when sent to
 * the ActionComponent (fullUid=0x000500). Mirrors the internal EngineStats
 * counters that track watchpoint, sequence, and action activity.
 *
 * Wire format: little-endian, packed, 56 bytes total.
 *
 * @note RT-safe: Pure data structure, no allocation or I/O.
 */

#include <cstdint>

namespace system_core {
namespace action {

/* ----------------------------- ActionTlmOpcode ----------------------------- */

/// Telemetry opcodes for ActionComponent (component-specific range 0x0100+).
enum class ActionTlmOpcode : std::uint16_t {
  GET_STATS = 0x0100 ///< Request action engine statistics snapshot.
};

/* ----------------------------- ActionHealthTlm ----------------------------- */

/**
 * @struct ActionHealthTlm
 * @brief Action engine statistics telemetry payload.
 *
 * Returned as response payload to GET_STATS (opcode 0x0100) when
 * addressed to the ActionComponent. All counters are cumulative
 * since startup or last reset.
 *
 * Wire format (56 bytes, little-endian):
 *   Offset  Size  Field
 *   --- Cycle Stats ---
 *   0       4     totalCycles            - processCycle() invocations
 *   --- Watchpoint/Group Activity ---
 *   4       4     watchpointsFired       - Watchpoint edge detections
 *   8       4     groupsFired            - Group edge detections
 *   --- Action Activity ---
 *   12      4     actionsApplied         - Actions applied
 *   16      4     commandsRouted         - COMMAND actions routed
 *   20      4     armControlsApplied     - ARM_CONTROL actions applied
 *   --- Sequence Activity ---
 *   24      4     sequenceSteps          - Sequence step actions fired
 *   28      4     sequenceTimeouts       - Step timeouts fired
 *   32      4     sequenceRetries        - Step retries
 *   36      4     sequenceAborts         - Sequence aborts from timeout
 *   --- Notification Activity ---
 *   40      4     notificationsInvoked   - Notification callbacks invoked
 *   --- Failures ---
 *   44      4     resolveFailures        - DataTarget resolution failures
 *   --- Loading ---
 *   48      4     rtsLoaded              - Standalone RTS files loaded
 *   52      4     atsLoaded              - Standalone ATS files loaded
 *   --- Abort/Exclusion ---
 *   56      4     abortEventsDispatched  - Abort events dispatched
 *   60      4     exclusionStops         - Sequences stopped by mutual exclusion
 *
 * Total: 64 bytes.
 */
struct __attribute__((packed)) ActionHealthTlm {
  /* ----------------------------- Cycle Stats ----------------------------- */

  std::uint32_t totalCycles{0}; ///< processCycle() invocations.

  /* ----------------------------- Watchpoint/Group ----------------------------- */

  std::uint32_t watchpointsFired{0}; ///< Watchpoint edge detections.
  std::uint32_t groupsFired{0};      ///< Group edge detections.

  /* ----------------------------- Actions ----------------------------- */

  std::uint32_t actionsApplied{0};     ///< Actions applied.
  std::uint32_t commandsRouted{0};     ///< COMMAND actions routed.
  std::uint32_t armControlsApplied{0}; ///< ARM_CONTROL actions applied.

  /* ----------------------------- Sequences ----------------------------- */

  std::uint32_t sequenceSteps{0};    ///< Sequence step actions fired.
  std::uint32_t sequenceTimeouts{0}; ///< Step timeouts fired.
  std::uint32_t sequenceRetries{0};  ///< Step retries.
  std::uint32_t sequenceAborts{0};   ///< Sequence aborts from timeout.

  /* ----------------------------- Notifications ----------------------------- */

  std::uint32_t notificationsInvoked{0}; ///< Notification callbacks invoked.

  /* ----------------------------- Failures ----------------------------- */

  std::uint32_t resolveFailures{0}; ///< DataTarget resolution failures.

  /* ----------------------------- Loading ----------------------------- */

  std::uint32_t rtsLoaded{0}; ///< Standalone RTS files loaded.
  std::uint32_t atsLoaded{0}; ///< Standalone ATS files loaded.

  /* ----------------------------- Abort/Exclusion ----------------------------- */

  std::uint32_t abortEventsDispatched{0}; ///< Abort events dispatched.
  std::uint32_t exclusionStops{0};        ///< Sequences stopped by mutual exclusion.
};

static_assert(sizeof(ActionHealthTlm) == 64, "ActionHealthTlm size mismatch");

} // namespace action
} // namespace system_core

#endif // APEX_SYSTEM_CORE_ACTION_ACTION_TLM_HPP
