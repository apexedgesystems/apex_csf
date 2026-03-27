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

#include <cstdint>

namespace system_core {
namespace support {

/* ----------------------------- SysMonTlmOpcode ----------------------------- */

/// Telemetry opcodes for SystemMonitor (component-specific range 0x0100+).
enum class SysMonTlmOpcode : std::uint16_t {
  HEALTH_SAMPLE = 0x0100 ///< Periodic health telemetry snapshot.
};

/* ----------------------------- SysMonHealthTlm ----------------------------- */

/**
 * @struct SysMonHealthTlm
 * @brief Periodic health telemetry payload.
 *
 * Sent once per telemetry() invocation. Contains latest sampled values
 * for all enabled domains.
 *
 * Wire format (88 bytes, little-endian):
 *   Offset  Size  Field
 *   0       4     sampleCount       - Total samples taken
 *   4       4     warnCount         - Total warning breaches
 *   8       4     critCount         - Total critical breaches
 *   --- CPU ---
 *   12      1     monitoredCoreCount - Number of valid coreLoad entries
 *   13      1     pad0              - Alignment
 *   14      2     cpuTempC          - CPU temperature (Celsius, -1 = N/A)
 *   16      64    coreLoad[16]      - Per-core load (float percent, 0-100)
 *   --- Memory ---
 *   80      4     ramUsedPercent    - RAM utilization (float, 0-100)
 *   84      4     fdCount           - Open file descriptor count
 *
 * Total: 88 bytes.
 */
struct __attribute__((packed)) SysMonHealthTlm {
  /* ----------------------------- Counters ----------------------------- */

  std::uint32_t sampleCount{0}; ///< Total telemetry samples since init.
  std::uint32_t warnCount{0};   ///< Cumulative warning threshold breaches.
  std::uint32_t critCount{0};   ///< Cumulative critical threshold breaches.

  /* ----------------------------- CPU Domain ----------------------------- */

  std::uint8_t monitoredCoreCount{0};    ///< Number of valid entries in coreLoad[].
  std::uint8_t pad0{0};                  ///< Alignment padding.
  std::int16_t cpuTempC{-1};             ///< CPU temperature (Celsius). -1 = unavailable.
  float coreLoad[MAX_MONITORED_CORES]{}; ///< Per-core load (percent, 0-100).

  /* ----------------------------- Memory Domain ----------------------------- */

  float ramUsedPercent{0.0F}; ///< RAM utilization (percent, 0-100).
  std::uint32_t fdCount{0};   ///< Open file descriptor count.
};

static_assert(sizeof(SysMonHealthTlm) == 88, "SysMonHealthTlm size mismatch");

} // namespace support
} // namespace system_core

#endif // APEX_SUPPORT_SYSTEM_MONITOR_TLM_HPP
