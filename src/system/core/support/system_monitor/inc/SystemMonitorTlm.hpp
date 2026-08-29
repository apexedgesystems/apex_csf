#ifndef APEX_SUPPORT_SYSTEM_MONITOR_TLM_HPP
#define APEX_SUPPORT_SYSTEM_MONITOR_TLM_HPP
/**
 * @file SystemMonitorTlm.hpp
 * @brief Telemetry wire format for SystemMonitor periodic health data.
 *
 * Packed POD struct sent via postInternalTelemetry() at the configured
 * sample rate (default 1 Hz). The Interface component wraps this in
 * APROTO and forwards to external TCP clients.
 *
 * Wire format: little-endian, packed, 88 bytes total.
 *
 * @note RT-safe: Pure data structure, no allocation or I/O.
 */

#include "src/system/core/support/system_monitor/inc/SystemMonitorConfig.hpp"

#include "src/system/core/support/system_monitor/.auto/SysMonHealthTlm_auto.hpp"

#include <cstdint>

namespace system_core {
namespace support {

/* ----------------------------- SysMonTlmOpcode ----------------------------- */

/// Telemetry opcodes for SystemMonitor (component-specific range 0x0100+).
enum class SysMonTlmOpcode : std::uint16_t {
  HEALTH_SAMPLE = 0x0100 ///< Periodic health telemetry snapshot.
};

/* ----------------------------- SysMonHealthTlm ----------------------------- */

} // namespace support
} // namespace system_core

#endif // APEX_SUPPORT_SYSTEM_MONITOR_TLM_HPP
