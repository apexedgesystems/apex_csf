#ifndef APEX_SUPPORT_SYSTEM_MONITOR_HPP
#define APEX_SUPPORT_SYSTEM_MONITOR_HPP
/**
 * @file SystemMonitor.hpp
 * @brief Runtime system health monitor (SUPPORT component).
 *
 * Provides two phases of monitoring:
 *
 * 1. Init Snapshot (NOT RT-safe, runs once in doInit()):
 *    Captures static system properties: kernel version, RT config, CPU
 *    topology, memory limits, GPU info. Logged at startup.
 *
 * 2. Runtime Telemetry (RT-safe, scheduled task at configurable rate):
 *    Periodically samples: per-core CPU utilization, CPU temperature,
 *    RAM/swap usage, FD count, RT throttle count, and optionally GPU
 *    telemetry. Compares against configurable warn/crit thresholds.
 *
 * Domains (individually enabled via TPRM):
 *   - CPU: per-core load, temperature, RT throttle events
 *   - Memory: RAM utilization, swap usage, FD exhaustion
 *   - GPU: temperature, utilization, memory, throttle state (CUDA only)
 *
 * componentId = 200  (support component range)
 * fullUid = 0xC800   (single instance, instanceIndex=0)
 *
 * @note NOT RT-safe in doInit(). telemetry() task is RT-safe.
 */

#include "src/system/core/support/system_monitor/inc/SystemMonitorConfig.hpp"
#include "src/system/core/support/system_monitor/inc/SystemMonitorData.hpp"
#include "src/system/core/support/system_monitor/inc/SystemMonitorSnapshot.hpp"

#include "src/system/core/infrastructure/system_component/apex/inc/SupportComponentBase.hpp"

#include <cstdint>

#include <filesystem>

// Forward-declare seeker types to avoid header inclusion in this header.
// The .cpp includes the actual seeker headers.
namespace seeker {
namespace cpu {
struct CpuUtilizationSnapshot;
} // namespace cpu
} // namespace seeker

namespace system_core {
namespace support {

using system_core::system_component::Status;
using system_core::system_component::SupportComponentBase;

/* ----------------------------- SystemMonitor ----------------------------- */

/**
 * @class SystemMonitor
 * @brief Runtime system health monitor with threshold alerting.
 *
 * Scheduled task:
 *   - telemetry (configurable, default 1 Hz): Sample system metrics,
 *     compare against thresholds, log warnings/errors.
 *
 * Usage:
 * @code
 *   SystemMonitor monitor;
 *   monitor.setConfig(config);    // Optional: override defaults
 *   executive.registerSupport(&monitor);
 *   // ... executive runs telemetry task at configured rate
 *
 *   // Query results:
 *   monitor.snapshot();           // Init-time system info
 *   monitor.warnCount();          // Total threshold warnings
 *   monitor.critCount();          // Total critical alerts
 * @endcode
 */
class SystemMonitor final : public SupportComponentBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  static constexpr std::uint16_t COMPONENT_ID = 200;
  static constexpr const char* COMPONENT_NAME = "SystemMonitor";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }

  /* ----------------------------- Task UIDs ----------------------------- */

  enum class TaskUid : std::uint8_t {
    TELEMETRY = 1 ///< Periodic system health check.
  };

  /* ----------------------------- Construction ----------------------------- */

  SystemMonitor() noexcept;
  ~SystemMonitor() override;

  /* ----------------------------- Configuration ----------------------------- */

  /**
   * @brief Set monitor configuration (thresholds, enabled domains).
   * @param config Configuration struct.
   * @note Must be called before init() if overriding defaults.
   * @note NOT RT-safe.
   */
  void setConfig(const SystemMonitorConfig& config) noexcept { config_ = config; }

  /**
   * @brief Get current configuration.
   * @return Reference to active config.
   * @note RT-safe.
   */
  [[nodiscard]] const SystemMonitorConfig& config() const noexcept { return config_; }

  /**
   * @brief Load tunable parameters from TPRM directory.
   * @param tprmDir Path to directory containing extracted TPRMs.
   * @return true on success, false if TPRM not found (uses defaults).
   * @pre Must call setInstanceIndex() first to assign UID.
   * @note NOT RT-safe.
   */
  bool loadTprm(const std::filesystem::path& tprmDir) noexcept override;

  /* ----------------------------- Task Methods ----------------------------- */

  /**
   * @brief Periodic telemetry collection and threshold check.
   *
   * For each enabled domain:
   *   CPU:    Snapshot /proc/stat, compute delta, check per-core load.
   *           Read thermal zone temp. Check RT throttle count delta.
   *   Memory: Read /proc/meminfo, check RAM/swap usage. Check FD count.
   *   GPU:    Read NVML telemetry, check temp/util/throttle.
   *
   * Logs warnings on threshold breaches. Increments warn/crit counters.
   *
   * @return 0 on success.
   * @note RT-safe: All seeker calls used here are RT-safe (bounded file
   *       reads, no allocation, no directory iteration).
   */
  std::uint8_t telemetry() noexcept;

  [[nodiscard]] const char* label() const noexcept override { return "SYS_MON"; }

  /* ----------------------------- Accessors ----------------------------- */

  /// Init-time system snapshot.
  [[nodiscard]] const SystemMonitorSnapshot& snapshot() const noexcept { return snapshot_; }

  /// Total warning threshold breaches since init.
  [[nodiscard]] std::uint32_t warnCount() const noexcept { return warnCount_; }

  /// Total critical threshold breaches since init.
  [[nodiscard]] std::uint32_t critCount() const noexcept { return critCount_; }

  /// Total telemetry samples taken.
  [[nodiscard]] std::uint32_t sampleCount() const noexcept { return sampleCount_; }

  /* ----------------------------- Latest Telemetry ----------------------------- */

  /// Latest per-core CPU load (percent, 0-100). Index = monitoredCores[i].
  [[nodiscard]] float lastCpuLoad(std::size_t monitoredIdx) const noexcept;

  /// Latest CPU temperature (Celsius). -1 if unavailable.
  [[nodiscard]] std::int16_t lastCpuTempC() const noexcept { return lastCpuTempC_; }

  /// Latest RAM utilization (percent, 0-100).
  [[nodiscard]] float lastRamUsedPercent() const noexcept { return lastRamUsedPercent_; }

  /// Latest swap used (bytes).
  [[nodiscard]] std::uint64_t lastSwapUsedBytes() const noexcept { return lastSwapUsedBytes_; }

  /// Latest open FD count.
  [[nodiscard]] std::uint32_t lastFdCount() const noexcept { return lastFdCount_; }

  /// Latest GPU temperature (Celsius). -1 if unavailable.
  [[nodiscard]] std::int16_t lastGpuTempC() const noexcept { return lastGpuTempC_; }

  /// Latest GPU utilization (percent, 0-100). -1 if unavailable.
  [[nodiscard]] std::int8_t lastGpuUtilPercent() const noexcept { return lastGpuUtilPercent_; }

protected:
  /* ----------------------------- Lifecycle ----------------------------- */

  [[nodiscard]] std::uint8_t doInit() noexcept override;

private:
  /* ----------------------------- Init Helpers ----------------------------- */

  void captureKernelSnapshot() noexcept;
  void captureCpuSnapshot() noexcept;
  void captureMemorySnapshot() noexcept;
  void captureGpuSnapshot() noexcept;
  void logSnapshot() noexcept;

  /* ----------------------------- Telemetry Helpers ----------------------------- */

  void sampleCpu() noexcept;
  void sampleMemory() noexcept;
  void sampleGpu() noexcept;

  /* ----------------------------- State ----------------------------- */

  SystemMonitorConfig config_;
  SystemMonitorSnapshot snapshot_;

  /// Counters.
  std::uint32_t warnCount_{0};
  std::uint32_t critCount_{0};
  std::uint32_t sampleCount_{0};

  /// CPU utilization snapshot (previous sample for delta computation).
  /// Opaque storage to avoid seeker header in this header.
  /// Actual type: seeker::cpu::CpuUtilizationSnapshot (aligned, ~82 KB).
  struct CpuSnapStorage;
  CpuSnapStorage* cpuSnap_{nullptr};

  /// Latest telemetry values.
  float lastCoreLoad_[MAX_MONITORED_CORES]{};
  std::int16_t lastCpuTempC_{-1};
  float lastRamUsedPercent_{0.0F};
  std::uint64_t lastSwapUsedBytes_{0};
  std::uint32_t lastFdCount_{0};
  float lastFdUsedPercent_{0.0F};
  std::int16_t lastGpuTempC_{-1};      // NOLINT(misc-non-private-member-variables-in-classes)
  std::int8_t lastGpuUtilPercent_{-1}; // NOLINT(misc-non-private-member-variables-in-classes)
  [[maybe_unused]] float lastGpuMemUsedPercent_{0.0F};

  /// RT throttle tracking (cumulative from /proc/sys/kernel/sched_rt_throttled).
  std::uint64_t prevRtThrottleCount_{0};
};

} // namespace support
} // namespace system_core

#endif // APEX_SUPPORT_SYSTEM_MONITOR_HPP
