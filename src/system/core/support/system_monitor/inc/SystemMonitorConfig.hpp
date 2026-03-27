#ifndef APEX_SUPPORT_SYSTEM_MONITOR_CONFIG_HPP
#define APEX_SUPPORT_SYSTEM_MONITOR_CONFIG_HPP
/**
 * @file SystemMonitorConfig.hpp
 * @brief Threshold configuration for the SystemMonitor support component.
 *
 * All thresholds are TPRM-loadable. Each domain can be independently enabled
 * or disabled. Warn thresholds generate log warnings; crit thresholds generate
 * log errors and increment critical counters.
 *
 * Defaults are conservative values suitable for most autonomous systems.
 *
 * @note RT-safe: Pure data structure, no allocation or I/O.
 */

#include <cstddef>
#include <cstdint>

namespace system_core {
namespace support {

/* ----------------------------- Constants ----------------------------- */

/// Maximum number of monitored CPU cores.
inline constexpr std::size_t MAX_MONITORED_CORES = 16;

/* ----------------------------- CpuThresholds ----------------------------- */

/**
 * @brief CPU domain thresholds and configuration.
 *
 * Monitors per-core utilization on specific cores (typically the cores
 * the executive pins its threads to). Also monitors CPU temperature
 * and RT scheduler throttle events.
 */
struct CpuThresholds {
  bool enabled{true};

  /// Cores to monitor (indices into /proc/stat per-core data).
  std::uint16_t monitoredCores[MAX_MONITORED_CORES]{};
  std::uint8_t monitoredCoreCount{0};

  /// Per-core CPU load thresholds (percent, 0-100).
  float loadWarnPercent{80.0F};
  float loadCritPercent{95.0F};

  /// CPU temperature thresholds (Celsius).
  /// Read from /sys/class/thermal/thermal_zone0/temp (RT-safe, single file).
  std::int16_t tempWarnC{75};
  std::int16_t tempCritC{85};

  /// RT scheduler throttle count delta per sample.
  /// Any throttle is concerning for an RT system.
  std::uint32_t rtThrottleWarn{1};
  std::uint32_t rtThrottleCrit{10};
};

/* ----------------------------- MemoryThresholds ----------------------------- */

/**
 * @brief Memory domain thresholds.
 *
 * Monitors RAM utilization, swap usage, and memlock budget consumption.
 * On RT systems, any swap usage is typically a critical event.
 */
struct MemoryThresholds {
  bool enabled{true};

  /// RAM utilization thresholds (percent, 0-100).
  float ramUsedWarnPercent{80.0F};
  float ramUsedCritPercent{95.0F};

  /// Swap usage thresholds (bytes). Default: any swap = critical.
  std::uint64_t swapUsedWarnBytes{1};
  std::uint64_t swapUsedCritBytes{1};

  /// File descriptor utilization thresholds (percent, 0-100).
  float fdUsedWarnPercent{70.0F};
  float fdUsedCritPercent{90.0F};
};

/* ----------------------------- GpuThresholds ----------------------------- */

/**
 * @brief GPU domain thresholds (NVIDIA via NVML).
 *
 * Monitors GPU temperature, compute utilization, memory utilization,
 * and throttle state. Only active on CUDA-enabled builds with an
 * NVIDIA GPU present.
 */
struct GpuThresholds {
  bool enabled{false}; ///< Off by default (not all platforms have GPU).

  /// GPU device index to monitor (0 for single-GPU systems).
  std::int8_t deviceIndex{0};

  /// GPU temperature thresholds (Celsius).
  std::int16_t tempWarnC{75};
  std::int16_t tempCritC{85};

  /// GPU compute utilization thresholds (percent, 0-100).
  float utilWarnPercent{90.0F};
  float utilCritPercent{98.0F};

  /// GPU memory utilization thresholds (percent, 0-100).
  float memUsedWarnPercent{80.0F};
  float memUsedCritPercent{95.0F};

  /// Any throttle active = flag.
  bool throttleWarnOnAny{true};
};

/* ----------------------------- SystemMonitorConfig ----------------------------- */

/**
 * @brief Top-level configuration for the SystemMonitor component.
 *
 * Loadable from TPRM. Contains per-domain enable flags and thresholds.
 */
struct SystemMonitorConfig {
  /// Telemetry task sample rate (Hz). Default 1 Hz.
  float sampleRateHz{1.0F};

  /// Domain configurations.
  CpuThresholds cpu;
  MemoryThresholds memory;
  GpuThresholds gpu;
};

} // namespace support
} // namespace system_core

#endif // APEX_SUPPORT_SYSTEM_MONITOR_CONFIG_HPP
