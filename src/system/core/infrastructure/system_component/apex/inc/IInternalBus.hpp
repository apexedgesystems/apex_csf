#ifndef APEX_SYSTEM_CORE_IINTERNAL_BUS_HPP
#define APEX_SYSTEM_CORE_IINTERNAL_BUS_HPP
/**
 * @file IInternalBus.hpp
 * @brief Abstract interface for internal component-to-component messaging.
 *
 * Design:
 *   - Minimal interface for sending internal commands and telemetry.
 *   - Implemented by ApexInterface (owns the APROTO encoding and queue routing).
 *   - Components depend on this abstraction, not the concrete interface.
 *
 * This interface exists to break the dependency between system_component and
 * the concrete ApexInterface implementation. Components can send internal
 * messages without knowing about the interface component.
 *
 * Pattern mirrors IComponentResolver (reverse direction).
 */

#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <cstdint>

namespace system_core {
namespace system_component {

/* ----------------------------- IInternalBus ----------------------------- */

/**
 * @class IInternalBus
 * @brief Abstract interface for internal component messaging.
 *
 * Implementers:
 *   - ApexInterface (system_core::interface)
 *
 * Usage:
 *   - Components get IInternalBus* via setInternalBus() during registration.
 *   - Components call postInternalCommand() to send messages to other components.
 *   - Components call postInternalTelemetry() to send telemetry to external interface.
 *
 * @note RT-safe: Implementations must ensure methods are RT-safe (lock-free queues).
 */
class IInternalBus {
public:
  virtual ~IInternalBus() = default;

  /**
   * @brief Post internal command to another component.
   * @param srcFullUid Source component's fullUid.
   * @param dstFullUid Target component's fullUid.
   * @param opcode Command opcode.
   * @param payload Command payload.
   * @return true if queued successfully, false if queue full or component not found.
   * @note RT-safe: Lock-free queue push, no allocation.
   * @note Message is routed directly to target component's cmdInbox.
   * @note Target component receives message via handleCommand() on next tick.
   */
  [[nodiscard]] virtual bool
  postInternalCommand(std::uint32_t srcFullUid, std::uint32_t dstFullUid, std::uint16_t opcode,
                      apex::compat::rospan<std::uint8_t> payload) noexcept = 0;

  /**
   * @brief Post internal telemetry to external interface (broadcast to socket).
   * @param srcFullUid Source component's fullUid.
   * @param opcode Telemetry opcode.
   * @param payload Telemetry payload.
   * @return true if queued successfully, false if queue full.
   * @note RT-safe: Lock-free queue push, no allocation.
   * @note Telemetry is broadcast to external socket clients.
   */
  [[nodiscard]] virtual bool
  postInternalTelemetry(std::uint32_t srcFullUid, std::uint16_t opcode,
                        apex::compat::rospan<std::uint8_t> payload) noexcept = 0;

  /**
   * @brief Post command to multiple components (multicast).
   * @param srcFullUid Source component's fullUid.
   * @param dstFullUids Span of target component fullUids.
   * @param opcode Command opcode.
   * @param payload Command payload (shared read-only by all recipients).
   * @return Number of components successfully queued.
   * @note RT-safe: Lock-free operations, zero-copy (single buffer, refcounted).
   * @note Recipients receive read-only access; must copy if persistence needed.
   */
  [[nodiscard]] virtual std::size_t
  postMulticastCommand(std::uint32_t srcFullUid, apex::compat::rospan<std::uint32_t> dstFullUids,
                       std::uint16_t opcode,
                       apex::compat::rospan<std::uint8_t> payload) noexcept = 0;

  /**
   * @brief Post command to all registered components (broadcast).
   * @param srcFullUid Source component's fullUid.
   * @param opcode Command opcode.
   * @param payload Command payload (shared read-only by all recipients).
   * @return Number of components successfully queued.
   * @note RT-safe: Lock-free operations, zero-copy.
   * @note Excludes source component from recipients.
   */
  [[nodiscard]] virtual std::size_t
  postBroadcastCommand(std::uint32_t srcFullUid, std::uint16_t opcode,
                       apex::compat::rospan<std::uint8_t> payload) noexcept = 0;
};

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_CORE_IINTERNAL_BUS_HPP
