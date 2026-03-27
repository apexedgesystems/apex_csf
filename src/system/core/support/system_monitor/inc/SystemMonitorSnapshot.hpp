#ifndef APEX_SUPPORT_SYSTEM_MONITOR_SNAPSHOT_HPP
#define APEX_SUPPORT_SYSTEM_MONITOR_SNAPSHOT_HPP
/**
 * @file SystemMonitorSnapshot.hpp
 * @brief Init-time system snapshot captured once during doInit().
 *
 * Provides the "here is what you are running on" report. Contains static
 * system properties that do not change at runtime. Logged at startup so
 * the developer has full context when reviewing runtime telemetry.
 *
 * @note NOT RT-safe: Populated during doInit() using non-RT seeker calls.
 */

#include <cstddef>
#include <cstdint>

#include <array>

namespace system_core {
namespace support {

/* ----------------------------- Constants ----------------------------- */

inline constexpr std::size_t SNAPSHOT_STRING_SIZE = 128;
inline constexpr std::size_t SNAPSHOT_SHORT_SIZE = 32;

/* ----------------------------- KernelSnapshot ----------------------------- */

/**
 * @brief Kernel and RT configuration captured at init.
 */
struct KernelSnapshot {
  std::array<char, SNAPSHOT_STRING_SIZE> release{};     ///< e.g., "6.1.0-rt5-amd64"
  std::array<char, SNAPSHOT_SHORT_SIZE> preemptModel{}; ///< e.g., "PREEMPT_RT"
  bool isRtKernel{false};                               ///< CONFIG_PREEMPT_RT=y
  bool nohzFull{false};                                 ///< nohz_full= on cmdline
  bool isolCpus{false};                                 ///< isolcpus= on cmdline
  bool highResTimers{false};                            ///< High-resolution timers enabled
};

/* ----------------------------- CpuSnapshot ----------------------------- */

/**
 * @brief CPU topology and configuration captured at init.
 */
struct CpuSnapshot {
  std::uint16_t coreCount{0};                          ///< Total logical cores
  std::array<char, SNAPSHOT_STRING_SIZE> modelName{};  ///< e.g., "ARM Cortex-A78AE"
  std::array<char, SNAPSHOT_SHORT_SIZE> clockSource{}; ///< e.g., "tsc", "arch_sys_counter"

  /// RT scheduler bandwidth config.
  double rtBandwidthPercent{0.0}; ///< e.g., 95.0 means 950ms/1000ms
  bool rtBandwidthUnlimited{false};
};

/* ----------------------------- MemorySnapshot ----------------------------- */

/**
 * @brief Memory configuration captured at init.
 */
struct MemorySnapshot {
  std::uint64_t totalRamBytes{0};
  std::uint64_t totalSwapBytes{0};
  int swappiness{-1};       ///< vm.swappiness (0-100)
  int overcommitMemory{-1}; ///< vm.overcommit_memory (0-2)

  /// Memlock limits.
  std::uint64_t memlockSoftBytes{0};
  std::uint64_t memlockHardBytes{0};
  bool memlockUnlimited{false};

  /// Process limits.
  std::uint64_t fdSoftLimit{0};
  std::uint64_t fdHardLimit{0};
  bool canUseRtScheduling{false};
};

/* ----------------------------- GpuSnapshot ----------------------------- */

/**
 * @brief GPU configuration captured at init (NVIDIA only).
 */
struct GpuSnapshot {
  bool available{false};
  std::array<char, SNAPSHOT_STRING_SIZE> deviceName{}; ///< e.g., "NVIDIA Orin"
  std::array<char, SNAPSHOT_SHORT_SIZE> computeCap{};  ///< e.g., "8.7"
  std::uint64_t totalMemoryBytes{0};
  std::int16_t shutdownTempC{0}; ///< Max temp before forced shutdown
  std::int16_t slowdownTempC{0}; ///< Max temp before clock throttle
};

/* ----------------------------- SystemMonitorSnapshot ----------------------------- */

/**
 * @brief Complete system snapshot captured once at init.
 *
 * This is a pure data struct. The SystemMonitor populates it in doInit()
 * and logs it. It can also be queried by other components for system info.
 */
struct SystemMonitorSnapshot {
  KernelSnapshot kernel;
  CpuSnapshot cpu;
  MemorySnapshot memory;
  GpuSnapshot gpu;
};

} // namespace support
} // namespace system_core

#endif // APEX_SUPPORT_SYSTEM_MONITOR_SNAPSHOT_HPP
