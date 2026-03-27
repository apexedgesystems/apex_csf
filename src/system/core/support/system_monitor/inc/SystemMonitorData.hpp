#ifndef APEX_SUPPORT_SYSTEM_MONITOR_DATA_HPP
#define APEX_SUPPORT_SYSTEM_MONITOR_DATA_HPP
/**
 * @file SystemMonitorData.hpp
 * @brief Packed TPRM data structure for SystemMonitor configuration.
 *
 * Flat packed struct for binary TPRM loading via hex2cpp(). The loadTprm()
 * method unpacks this into the runtime SystemMonitorConfig.
 *
 * Size: 122 bytes.
 *
 * @note RT-safe: Pure data structure, no allocation or I/O.
 */

#include <cstdint>

namespace system_core {
namespace support {

/* ----------------------------- SystemMonitorTunableParams ----------------------------- */

/**
 * @struct SystemMonitorTunableParams
 * @brief TPRM-loadable configuration for SystemMonitor.
 *
 * Flat packed layout for binary serialization. All multi-byte fields are
 * little-endian (host byte order). Boolean fields use uint8_t (0/1).
 *
 * Layout:
 *   [0..3]    sampleRateHz         float  (4)
 *   --- CPU domain ---
 *   [4]       cpuEnabled           u8     (1)
 *   [5]       monitoredCoreCount   u8     (1)
 *   [6..37]   monitoredCores[16]   u16x16 (32)
 *   [38..41]  cpuLoadWarnPercent   float  (4)
 *   [42..45]  cpuLoadCritPercent   float  (4)
 *   [46..47]  cpuTempWarnC         i16    (2)
 *   [48..49]  cpuTempCritC         i16    (2)
 *   [50..53]  cpuRtThrottleWarn    u32    (4)
 *   [54..57]  cpuRtThrottleCrit    u32    (4)
 *   --- Memory domain ---
 *   [58]      memEnabled           u8     (1)
 *   [59..61]  memPad               u8x3   (3)
 *   [62..65]  memRamWarnPercent    float  (4)
 *   [66..69]  memRamCritPercent    float  (4)
 *   [70..77]  memSwapWarnBytes     u64    (8)
 *   [78..85]  memSwapCritBytes     u64    (8)
 *   [86..89]  memFdWarnPercent     float  (4)
 *   [90..93]  memFdCritPercent     float  (4)
 *   --- GPU domain ---
 *   [94]      gpuEnabled           u8     (1)
 *   [95]      gpuDeviceIndex       i8     (1)
 *   [96..97]  gpuTempWarnC         i16    (2)
 *   [98..99]  gpuTempCritC         i16    (2)
 *   [100..101] gpuPad              u16    (2)
 *   [102..105] gpuUtilWarnPercent  float  (4)
 *   [106..109] gpuUtilCritPercent  float  (4)
 *   [110..113] gpuMemWarnPercent   float  (4)
 *   [114..117] gpuMemCritPercent   float  (4)
 *   [118]     gpuThrottleWarnOnAny u8     (1)
 *   [119..121] gpuPad2             u8x3   (3)
 *
 * Total: 122 bytes.
 */
struct __attribute__((packed)) SystemMonitorTunableParams {
  /* ----------------------------- General ----------------------------- */

  float sampleRateHz{1.0F}; ///< Telemetry rate (Hz).

  /* ----------------------------- CPU Domain ----------------------------- */

  std::uint8_t cpuEnabled{1};          ///< 1 = enabled, 0 = disabled.
  std::uint8_t monitoredCoreCount{0};  ///< Number of valid entries in monitoredCores.
  std::uint16_t monitoredCores[16]{};  ///< Core indices to monitor.
  float cpuLoadWarnPercent{80.0F};     ///< Per-core load warning threshold.
  float cpuLoadCritPercent{95.0F};     ///< Per-core load critical threshold.
  std::int16_t cpuTempWarnC{75};       ///< CPU temperature warning (Celsius).
  std::int16_t cpuTempCritC{85};       ///< CPU temperature critical (Celsius).
  std::uint32_t cpuRtThrottleWarn{1};  ///< RT throttle delta warning threshold.
  std::uint32_t cpuRtThrottleCrit{10}; ///< RT throttle delta critical threshold.

  /* ----------------------------- Memory Domain ----------------------------- */

  std::uint8_t memEnabled{1};        ///< 1 = enabled, 0 = disabled.
  std::uint8_t memPad[3]{};          ///< Alignment padding.
  float memRamWarnPercent{80.0F};    ///< RAM utilization warning threshold.
  float memRamCritPercent{95.0F};    ///< RAM utilization critical threshold.
  std::uint64_t memSwapWarnBytes{1}; ///< Swap usage warning threshold (bytes).
  std::uint64_t memSwapCritBytes{1}; ///< Swap usage critical threshold (bytes).
  float memFdWarnPercent{70.0F};     ///< FD utilization warning threshold.
  float memFdCritPercent{90.0F};     ///< FD utilization critical threshold.

  /* ----------------------------- GPU Domain ----------------------------- */

  std::uint8_t gpuEnabled{0};           ///< 1 = enabled, 0 = disabled.
  std::int8_t gpuDeviceIndex{0};        ///< GPU device index.
  std::int16_t gpuTempWarnC{75};        ///< GPU temperature warning (Celsius).
  std::int16_t gpuTempCritC{85};        ///< GPU temperature critical (Celsius).
  std::uint16_t gpuPad{0};              ///< Alignment padding.
  float gpuUtilWarnPercent{90.0F};      ///< GPU utilization warning threshold.
  float gpuUtilCritPercent{98.0F};      ///< GPU utilization critical threshold.
  float gpuMemWarnPercent{80.0F};       ///< GPU memory warning threshold.
  float gpuMemCritPercent{95.0F};       ///< GPU memory critical threshold.
  std::uint8_t gpuThrottleWarnOnAny{1}; ///< 1 = warn on any GPU throttle.
  std::uint8_t gpuPad2[3]{};            ///< Alignment padding.
};

static_assert(sizeof(SystemMonitorTunableParams) == 122,
              "SystemMonitorTunableParams size mismatch");

} // namespace support
} // namespace system_core

#endif // APEX_SUPPORT_SYSTEM_MONITOR_DATA_HPP
