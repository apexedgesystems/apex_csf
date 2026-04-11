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

/**
 * @struct TelemetryManagerState
 * @brief Runtime state and counters.
 *
 * Size: 16 bytes.
 */
struct TelemetryManagerState {
  std::uint32_t collectCount{0}; ///< Total collect task invocations.
  std::uint32_t packetsSent{0};  ///< Total telemetry packets pushed to bus.
  std::uint32_t sendFailures{0}; ///< Failed pushes (queue full or registry miss).
  std::uint16_t activeCount{0};  ///< Number of active subscriptions.
  std::uint16_t reserved{0};     ///< Alignment padding.
};

static_assert(sizeof(TelemetryManagerState) == 16,
              "TelemetryManagerState size changed - update struct dictionary");

/* ----------------------------- TelemetryManagerHealthTlm ----------------------------- */

/**
 * @struct TelemetryManagerHealthTlm
 * @brief Health telemetry returned by GET_STATS (opcode 0x0100).
 *
 * Size: 16 bytes.
 */
struct __attribute__((packed)) TelemetryManagerHealthTlm {
  std::uint32_t collectCount{0};
  std::uint32_t packetsSent{0};
  std::uint32_t sendFailures{0};
  std::uint16_t activeCount{0};
  std::uint16_t reserved{0};
};

static_assert(sizeof(TelemetryManagerHealthTlm) == 16, "TelemetryManagerHealthTlm size changed");

} // namespace support
} // namespace system_core

#endif // APEX_SUPPORT_TELEMETRY_MANAGER_DATA_HPP
