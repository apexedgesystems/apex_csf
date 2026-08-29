#ifndef APEX_SUPPORT_TELEMETRY_MANAGER_DATA_HPP
#define APEX_SUPPORT_TELEMETRY_MANAGER_DATA_HPP
/**
 * @file TelemetryManagerData.hpp
 * @brief Data structures for TelemetryManager component.
 *
 * Contains:
 *  - TelemetrySubscription: Single telemetry channel definition (16 bytes)
 *  - TelemetryManagerTprm: TPRM-loadable subscription table (520 bytes)
 *  - TelemetryManagerState: Runtime state and counters (16 bytes)
 *
 * @note RT-safe: Pure data structures, no allocation or I/O.
 */

#include <cstdint>

#include "src/system/core/support/telemetry_manager/.auto/TelemetryManagerHealthTlm_auto.hpp"
#include "src/system/core/support/telemetry_manager/.auto/TelemetryManagerState_auto.hpp"

namespace system_core {
namespace support {

/* ----------------------------- Constants ----------------------------- */

/// Maximum telemetry subscriptions.
static constexpr std::size_t MAX_TELEMETRY_SUBSCRIPTIONS = 32;

/* ----------------------------- TelemetrySubscription ----------------------------- */

/**
 * @struct TelemetrySubscription
 * @brief Definition of a single telemetry push channel.
 *
 * Each subscription tells the TelemetryManager to read a data block
 * from the registry and push it via postInternalTelemetry() at a
 * configured rate.
 *
 * Size: 16 bytes.
 */
struct TelemetrySubscription {
  std::uint32_t fullUid{0}; ///< Target component fullUid (e.g., 0x00D000).
  std::uint8_t category{
      0};                  ///< DataCategory enum (0=STATIC, 1=TUNABLE, 2=STATE, 3=INPUT, 4=OUTPUT).
  std::uint8_t active{0};  ///< 1=active, 0=inactive.
  std::uint16_t opcode{0}; ///< APROTO opcode for outbound telemetry packet.
  std::uint16_t offset{0}; ///< Byte offset into data block (0=start).
  std::uint16_t length{0}; ///< Bytes to read (0=entire block).
  std::uint16_t rateDiv{1};  ///< Rate divisor: push every N collect ticks (1=every tick).
  std::uint16_t reserved{0}; ///< Alignment padding.
};

static_assert(sizeof(TelemetrySubscription) == 16,
              "TelemetrySubscription size changed - update TPRM template");

/* ----------------------------- TelemetryManagerTprm ----------------------------- */

/**
 * @struct TelemetryManagerTprm
 * @brief TPRM-loadable configuration for TelemetryManager.
 *
 * Contains the base collect rate and a fixed-size subscription table.
 * Unused subscriptions have active=0.
 *
 * Size: 8 + (32 * 16) = 520 bytes.
 */
struct TelemetryManagerTprm {
  std::uint16_t collectRateHz{1}; ///< Base collect task rate (Hz). Subscriptions divide from this.
  std::uint16_t reserved0{0};     ///< Alignment padding.
  std::uint32_t reserved1{0};     ///< Alignment padding.
  TelemetrySubscription subscriptions[MAX_TELEMETRY_SUBSCRIPTIONS]{};
};

static_assert(sizeof(TelemetryManagerTprm) == 520,
              "TelemetryManagerTprm size changed - update TPRM template");

/* ----------------------------- TelemetryManagerState ----------------------------- */

/* ----------------------------- TelemetryManagerHealthTlm ----------------------------- */

} // namespace support
} // namespace system_core

#endif // APEX_SUPPORT_TELEMETRY_MANAGER_DATA_HPP
