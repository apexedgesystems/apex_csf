/**
 * @file SystemMonitor.cpp
 * @brief Implementation of the SystemMonitor support component.
 *
 * Init phase uses non-RT seeker calls to capture system snapshot.
 * Runtime telemetry task uses only RT-safe seeker calls.
 */

#include "src/system/core/support/system_monitor/inc/SystemMonitor.hpp"
#include "src/system/core/support/system_monitor/inc/SystemMonitorTlm.hpp"
#include "src/system/core/infrastructure/system_component/apex/inc/IInternalBus.hpp"

// Seeker RT-safe APIs (used in telemetry task)
#include "src/cpu/inc/CpuUtilization.hpp" // getCpuUtilizationSnapshot, computeUtilizationDelta
#include "src/memory/inc/MemoryStats.hpp" // getMemoryStats

// Seeker NOT RT-safe APIs (used in doInit only)
#include "src/cpu/inc/CpuStats.hpp"         // readCpuInfo, readCpuCount
#include "src/system/inc/KernelInfo.hpp"    // getKernelInfo
#include "src/system/inc/ProcessLimits.hpp" // getProcessLimits
#include "src/system/inc/RtSchedConfig.hpp" // getRtSchedConfig
#include "src/timing/inc/ClockSource.hpp"   // getClockSource
#include "src/timing/inc/TimerConfig.hpp"   // getTimerConfig

// Seeker helpers (RT-safe file I/O)
#include "src/helpers/inc/Files.hpp"   // readFileInt
#include "src/helpers/inc/Strings.hpp" // copyToFixedArray

// TPRM loading
#include "src/utilities/helpers/inc/Files.hpp" // hex2cpp

// System FD monitoring
#include "src/system/inc/FileDescriptorStatus.hpp" // getOpenFdCount, getFdSoftLimit

#if COMPAT_NVML_AVAILABLE
#include "src/gpu/inc/GpuTelemetry.hpp"    // getGpuTelemetry (RT-safe for single device)
#include "src/gpu/inc/GpuTopology.hpp"     // getGpuTopology (NOT RT-safe, init only)
#include "src/gpu/inc/GpuMemoryStatus.hpp" // getGpuMemoryStatus (NOT RT-safe, init only)
#endif

#include <cstring>

#include <new>

#include <fmt/format.h>

namespace system_core {
namespace support {

/* ----------------------------- Constants ----------------------------- */

/// Path to CPU thermal zone 0 temperature (millidegrees Celsius).
/// This is a single file read, RT-safe on all Linux platforms.
static constexpr const char* THERMAL_ZONE0_PATH = "/sys/class/thermal/thermal_zone0/temp";

/// Path to RT throttle count in /proc/sys/kernel/sched_rt_throttled.
/// Not available on all kernels; returns 0 if missing.
static constexpr const char* RT_THROTTLE_PATH = "/proc/sys/kernel/sched_rt_throttled";

/* ----------------------------- CpuSnapStorage ----------------------------- */

/// Opaque wrapper around seeker::cpu::CpuUtilizationSnapshot.
/// Allocated once in doInit() so the telemetry task can compute deltas.
struct SystemMonitor::CpuSnapStorage {
  seeker::cpu::CpuUtilizationSnapshot snap;
};

/* ----------------------------- Construction ----------------------------- */

SystemMonitor::SystemMonitor() noexcept = default;

SystemMonitor::~SystemMonitor() {
  delete cpuSnap_;
  cpuSnap_ = nullptr;
}

/* ----------------------------- TPRM Loading ----------------------------- */

bool SystemMonitor::loadTprm(const std::filesystem::path& tprmDir) noexcept {
  if (!isRegistered()) {
    return false;
  }

  char filename[32];
  std::snprintf(filename, sizeof(filename), "%06x.tprm", fullUid());
  std::filesystem::path tprmPath = tprmDir / filename;

  if (!std::filesystem::exists(tprmPath)) {
    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(), "TPRM not found, using defaults");
    }
    return false;
  }

  std::string error;
  SystemMonitorTunableParams loaded{};
  if (!apex::helpers::files::hex2cpp(tprmPath.string(), loaded, error)) {
    auto* log = componentLog();
    if (log != nullptr) {
      log->warning(label(), 0, fmt::format("TPRM load failed: {}", error));
    }
    return false;
  }

  // Unpack into runtime config
  config_.sampleRateHz = loaded.sampleRateHz;

  // CPU domain
  config_.cpu.enabled = (loaded.cpuEnabled != 0);
  config_.cpu.monitoredCoreCount = loaded.monitoredCoreCount;
  const std::uint8_t CORE_COUNT = loaded.monitoredCoreCount > MAX_MONITORED_CORES
                                      ? static_cast<std::uint8_t>(MAX_MONITORED_CORES)
                                      : loaded.monitoredCoreCount;
  for (std::uint8_t i = 0; i < CORE_COUNT; ++i) {
    config_.cpu.monitoredCores[i] = loaded.monitoredCores[i];
  }
  config_.cpu.loadWarnPercent = loaded.cpuLoadWarnPercent;
  config_.cpu.loadCritPercent = loaded.cpuLoadCritPercent;
  config_.cpu.tempWarnC = loaded.cpuTempWarnC;
  config_.cpu.tempCritC = loaded.cpuTempCritC;
  config_.cpu.rtThrottleWarn = loaded.cpuRtThrottleWarn;
  config_.cpu.rtThrottleCrit = loaded.cpuRtThrottleCrit;

  // Memory domain
  config_.memory.enabled = (loaded.memEnabled != 0);
  config_.memory.ramUsedWarnPercent = loaded.memRamWarnPercent;
  config_.memory.ramUsedCritPercent = loaded.memRamCritPercent;
  config_.memory.swapUsedWarnBytes = loaded.memSwapWarnBytes;
  config_.memory.swapUsedCritBytes = loaded.memSwapCritBytes;
  config_.memory.fdUsedWarnPercent = loaded.memFdWarnPercent;
  config_.memory.fdUsedCritPercent = loaded.memFdCritPercent;

  // GPU domain
  config_.gpu.enabled = (loaded.gpuEnabled != 0);
  config_.gpu.deviceIndex = loaded.gpuDeviceIndex;
  config_.gpu.tempWarnC = loaded.gpuTempWarnC;
  config_.gpu.tempCritC = loaded.gpuTempCritC;
  config_.gpu.utilWarnPercent = loaded.gpuUtilWarnPercent;
  config_.gpu.utilCritPercent = loaded.gpuUtilCritPercent;
  config_.gpu.memUsedWarnPercent = loaded.gpuMemWarnPercent;
  config_.gpu.memUsedCritPercent = loaded.gpuMemCritPercent;
  config_.gpu.throttleWarnOnAny = (loaded.gpuThrottleWarnOnAny != 0);

  auto* log = componentLog();
  if (log != nullptr) {
    log->info(label(), fmt::format("TPRM loaded from {}", tprmPath.string()));
  }

  return true;
}

/* ----------------------------- Lifecycle ----------------------------- */

std::uint8_t SystemMonitor::doInit() noexcept {
  // Register telemetry task
  registerTask<SystemMonitor, &SystemMonitor::telemetry>(
      static_cast<std::uint8_t>(TaskUid::TELEMETRY), this, "telemetry");

  // Capture init-time snapshot (NOT RT-safe, cold path)
  captureKernelSnapshot();
  captureCpuSnapshot();
  captureMemorySnapshot();
  captureGpuSnapshot();

  // Allocate CPU utilization snapshot storage for delta computation
  if (config_.cpu.enabled) {
    cpuSnap_ = new (std::nothrow) CpuSnapStorage{};
    if (cpuSnap_ != nullptr) {
      cpuSnap_->snap = seeker::cpu::getCpuUtilizationSnapshot();
    }
  }

  // Capture initial RT throttle count for delta tracking
  prevRtThrottleCount_ =
      static_cast<std::uint64_t>(seeker::helpers::files::readFileInt64(RT_THROTTLE_PATH, 0));

  // Log the snapshot
  logSnapshot();

  return static_cast<std::uint8_t>(Status::SUCCESS);
}

/* ----------------------------- Init Helpers ----------------------------- */

void SystemMonitor::captureKernelSnapshot() noexcept {
  const auto INFO = seeker::system::getKernelInfo();

  seeker::helpers::strings::copyToFixedArray(snapshot_.kernel.release, INFO.release.data());
  seeker::helpers::strings::copyToFixedArray(snapshot_.kernel.preemptModel, INFO.preemptStr.data());
  snapshot_.kernel.isRtKernel = INFO.isRtKernel();
  snapshot_.kernel.nohzFull = INFO.nohzFull;
  snapshot_.kernel.isolCpus = INFO.isolCpus;

  const auto TIMER_CFG = seeker::timing::getTimerConfig();
  snapshot_.kernel.highResTimers = TIMER_CFG.highResTimersEnabled;
}

void SystemMonitor::captureCpuSnapshot() noexcept {
  const auto CPU_COUNT = seeker::cpu::readCpuCount();
  snapshot_.cpu.coreCount = static_cast<std::uint16_t>(CPU_COUNT.count);

  const auto CPU_INFO = seeker::cpu::readCpuInfo();
  seeker::helpers::strings::copyToFixedArray(snapshot_.cpu.modelName, CPU_INFO.model.data());

  const auto CLOCK = seeker::timing::getClockSource();
  seeker::helpers::strings::copyToFixedArray(snapshot_.cpu.clockSource, CLOCK.current.data());

  const auto RT_SCHED = seeker::system::getRtSchedConfig();
  snapshot_.cpu.rtBandwidthPercent = RT_SCHED.bandwidth.bandwidthPercent();
  snapshot_.cpu.rtBandwidthUnlimited = RT_SCHED.bandwidth.isUnlimited();
}

void SystemMonitor::captureMemorySnapshot() noexcept {
  const auto MEM = seeker::memory::getMemoryStats();
  snapshot_.memory.totalRamBytes = MEM.totalBytes;
  snapshot_.memory.totalSwapBytes = MEM.swapTotalBytes;
  snapshot_.memory.swappiness = MEM.swappiness;
  snapshot_.memory.overcommitMemory = MEM.overcommitMemory;

  const auto LIMITS = seeker::system::getProcessLimits();
  snapshot_.memory.memlockSoftBytes = LIMITS.memlock.soft;
  snapshot_.memory.memlockHardBytes = LIMITS.memlock.hard;
  snapshot_.memory.memlockUnlimited = LIMITS.memlock.unlimited;
  snapshot_.memory.fdSoftLimit = LIMITS.nofile.soft;
  snapshot_.memory.fdHardLimit = LIMITS.nofile.hard;
  snapshot_.memory.canUseRtScheduling = LIMITS.canUseRtScheduling();
}

void SystemMonitor::captureGpuSnapshot() noexcept {
#if COMPAT_NVML_AVAILABLE
  if (!config_.gpu.enabled) {
    return;
  }

  const auto TOPO = seeker::gpu::getGpuTopology();
  if (!TOPO.hasGpu()) {
    snapshot_.gpu.available = false;
    return;
  }

  snapshot_.gpu.available = true;
  const auto& DEV = TOPO.devices[static_cast<std::size_t>(config_.gpu.deviceIndex)];
  seeker::helpers::strings::copyToFixedArray(snapshot_.gpu.deviceName, DEV.name.c_str());
  seeker::helpers::strings::copyToFixedArray(snapshot_.gpu.computeCap,
                                             DEV.computeCapability().c_str());
  snapshot_.gpu.totalMemoryBytes = DEV.totalMemoryBytes;

  const auto TEL = seeker::gpu::getGpuTelemetry(config_.gpu.deviceIndex);
  snapshot_.gpu.shutdownTempC = static_cast<std::int16_t>(TEL.temperatureShutdownC);
  snapshot_.gpu.slowdownTempC = static_cast<std::int16_t>(TEL.temperatureSlowdownC);
#else
  snapshot_.gpu.available = false;
#endif
}

void SystemMonitor::logSnapshot() noexcept {
  auto* log = componentLog();
  if (log == nullptr) {
    return;
  }

  // Kernel
  log->info(label(), fmt::format("Kernel: {} preempt={} rt={}", snapshot_.kernel.release.data(),
                                 snapshot_.kernel.preemptModel.data(),
                                 snapshot_.kernel.isRtKernel ? "yes" : "no"));

  log->info(label(), fmt::format("  nohz_full={} isolcpus={} hrtimers={}",
                                 snapshot_.kernel.nohzFull ? "yes" : "no",
                                 snapshot_.kernel.isolCpus ? "yes" : "no",
                                 snapshot_.kernel.highResTimers ? "yes" : "no"));

  // CPU
  log->info(label(), fmt::format("CPU: {} cores model={} clock={}", snapshot_.cpu.coreCount,
                                 snapshot_.cpu.modelName.data(), snapshot_.cpu.clockSource.data()));

  log->info(label(), fmt::format("  RT bandwidth: {:.1f}%{}", snapshot_.cpu.rtBandwidthPercent,
                                 snapshot_.cpu.rtBandwidthUnlimited ? " (unlimited)" : ""));

  // Memory
  const double RAM_GB =
      static_cast<double>(snapshot_.memory.totalRamBytes) / (1024.0 * 1024.0 * 1024.0);
  log->info(label(), fmt::format("Memory: {:.1f}GB swap={}MB swappiness={} overcommit={}", RAM_GB,
                                 snapshot_.memory.totalSwapBytes / (1024 * 1024),
                                 snapshot_.memory.swappiness, snapshot_.memory.overcommitMemory));

  log->info(
      label(),
      fmt::format(
          "  memlock={}{} fd_limit={}/{} rt_sched={}",
          snapshot_.memory.memlockUnlimited ? "unlimited" : "",
          snapshot_.memory.memlockUnlimited
              ? ""
              : fmt::format("{}MB", snapshot_.memory.memlockSoftBytes / (1024 * 1024)).c_str(),
          snapshot_.memory.fdSoftLimit, snapshot_.memory.fdHardLimit,
          snapshot_.memory.canUseRtScheduling ? "yes" : "no"));

  // GPU
  if (snapshot_.gpu.available) {
    const double GPU_GB =
        static_cast<double>(snapshot_.gpu.totalMemoryBytes) / (1024.0 * 1024.0 * 1024.0);
    log->info(label(),
              fmt::format("GPU: {} compute={} mem={:.1f}GB shutdown={}C slowdown={}C",
                          snapshot_.gpu.deviceName.data(), snapshot_.gpu.computeCap.data(), GPU_GB,
                          snapshot_.gpu.shutdownTempC, snapshot_.gpu.slowdownTempC));
  } else {
    log->info(label(), "GPU: not available");
  }

  // Monitored cores
  if (config_.cpu.enabled && config_.cpu.monitoredCoreCount > 0) {
    std::string cores;
    for (std::uint8_t i = 0; i < config_.cpu.monitoredCoreCount; ++i) {
      if (i > 0) {
        cores += ',';
      }
      cores += fmt::format("{}", config_.cpu.monitoredCores[i]);
    }
    log->info(label(), fmt::format("Monitoring cores: [{}]", cores));
  }

  // Thresholds summary
  if (config_.cpu.enabled) {
    log->info(label(), fmt::format("CPU thresholds: load={}/{}% temp={}/{}C rtThrottle={}/{}",
                                   config_.cpu.loadWarnPercent, config_.cpu.loadCritPercent,
                                   config_.cpu.tempWarnC, config_.cpu.tempCritC,
                                   config_.cpu.rtThrottleWarn, config_.cpu.rtThrottleCrit));
  }
  if (config_.memory.enabled) {
    log->info(label(),
              fmt::format("Memory thresholds: ram={}/{}% swap={}/{}B fd={}/{}%",
                          config_.memory.ramUsedWarnPercent, config_.memory.ramUsedCritPercent,
                          config_.memory.swapUsedWarnBytes, config_.memory.swapUsedCritBytes,
                          config_.memory.fdUsedWarnPercent, config_.memory.fdUsedCritPercent));
  }
}

/* ----------------------------- Telemetry Task ----------------------------- */

std::uint8_t SystemMonitor::telemetry() noexcept {
  if (config_.cpu.enabled) {
    sampleCpu();
  }
  if (config_.memory.enabled) {
    sampleMemory();
  }
#if COMPAT_NVML_AVAILABLE
  if (config_.gpu.enabled && snapshot_.gpu.available) {
    sampleGpu();
  }
#endif

  ++sampleCount_;

  // Post structured telemetry via interface pipeline (RT-safe, zero-alloc at call site).
  auto* bus = internalBus();
  if (bus != nullptr) {
    SysMonHealthTlm tlm{};
    tlm.sampleCount = sampleCount_;
    tlm.warnCount = warnCount_;
    tlm.critCount = critCount_;
    tlm.monitoredCoreCount = config_.cpu.monitoredCoreCount;
    tlm.cpuTempC = lastCpuTempC_;
    for (std::uint8_t i = 0; i < config_.cpu.monitoredCoreCount; ++i) {
      tlm.coreLoad[i] = lastCoreLoad_[i];
    }
    tlm.ramUsedPercent = lastRamUsedPercent_;
    tlm.fdCount = lastFdCount_;

    const auto PAYLOAD = apex::compat::rospan<std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(&tlm), sizeof(tlm));
    (void)bus->postInternalTelemetry(
        fullUid(), static_cast<std::uint16_t>(SysMonTlmOpcode::HEALTH_SAMPLE), PAYLOAD);
  }

  // Event-level log: periodic summary only (not per-sample).
  if ((sampleCount_ % 10) == 0) {
    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(),
                fmt::format("samples={} warns={} crits={}", sampleCount_, warnCount_, critCount_));
    }
  }

  return 0;
}

/* ----------------------------- CPU Sampling ----------------------------- */

void SystemMonitor::sampleCpu() noexcept {
  auto* log = componentLog();

  // CPU utilization delta
  if (cpuSnap_ != nullptr) {
    const auto NOW = seeker::cpu::getCpuUtilizationSnapshot();
    const auto DELTA = seeker::cpu::computeUtilizationDelta(cpuSnap_->snap, NOW);

    // Check each monitored core
    for (std::uint8_t i = 0; i < config_.cpu.monitoredCoreCount; ++i) {
      const std::uint16_t CORE_IDX = config_.cpu.monitoredCores[i];
      if (CORE_IDX >= DELTA.coreCount) {
        continue;
      }

      const float LOAD = static_cast<float>(DELTA.perCore[CORE_IDX].active());
      lastCoreLoad_[i] = LOAD;

      if (LOAD >= config_.cpu.loadCritPercent) {
        ++critCount_;
        if (log != nullptr) {
          log->error(label(), 0,
                     fmt::format("core {} load {:.1f}% >= {:.0f}%", CORE_IDX, LOAD,
                                 config_.cpu.loadCritPercent));
        }
      } else if (LOAD >= config_.cpu.loadWarnPercent) {
        ++warnCount_;
        if (log != nullptr) {
          log->warning(label(), 0,
                       fmt::format("core {} load {:.1f}% >= {:.0f}%", CORE_IDX, LOAD,
                                   config_.cpu.loadWarnPercent));
        }
      }
    }

    // Store for next delta
    cpuSnap_->snap = NOW;
  }

  // CPU temperature (RT-safe: single file read)
  const std::int32_t TEMP_MILLI = seeker::helpers::files::readFileInt(THERMAL_ZONE0_PATH, -1000);
  if (TEMP_MILLI > -1000) {
    lastCpuTempC_ = static_cast<std::int16_t>(TEMP_MILLI / 1000);

    if (lastCpuTempC_ >= config_.cpu.tempCritC) {
      ++critCount_;
      if (log != nullptr) {
        log->error(label(), 0,
                   fmt::format("CPU temp {}C >= {}C", lastCpuTempC_, config_.cpu.tempCritC));
      }
    } else if (lastCpuTempC_ >= config_.cpu.tempWarnC) {
      ++warnCount_;
      if (log != nullptr) {
        log->warning(label(), 0,
                     fmt::format("CPU temp {}C >= {}C", lastCpuTempC_, config_.cpu.tempWarnC));
      }
    }
  }

  // RT throttle count delta (RT-safe: single file read)
  const auto CUR_THROTTLE =
      static_cast<std::uint64_t>(seeker::helpers::files::readFileInt64(RT_THROTTLE_PATH, 0));
  if (CUR_THROTTLE > prevRtThrottleCount_) {
    const std::uint64_t DELTA_THROTTLE = CUR_THROTTLE - prevRtThrottleCount_;

    if (DELTA_THROTTLE >= config_.cpu.rtThrottleCrit) {
      ++critCount_;
      if (log != nullptr) {
        log->error(
            label(), 0,
            fmt::format("RT throttle delta {} >= {}", DELTA_THROTTLE, config_.cpu.rtThrottleCrit));
      }
    } else if (DELTA_THROTTLE >= config_.cpu.rtThrottleWarn) {
      ++warnCount_;
      if (log != nullptr) {
        log->warning(
            label(), 0,
            fmt::format("RT throttle delta {} >= {}", DELTA_THROTTLE, config_.cpu.rtThrottleWarn));
      }
    }
  }
  prevRtThrottleCount_ = CUR_THROTTLE;
}

/* ----------------------------- Memory Sampling ----------------------------- */

void SystemMonitor::sampleMemory() noexcept {
  auto* log = componentLog();

  // RAM and swap (RT-safe)
  const auto MEM = seeker::memory::getMemoryStats();
  lastRamUsedPercent_ = static_cast<float>(MEM.utilizationPercent());
  lastSwapUsedBytes_ = MEM.swapUsedBytes();

  if (lastRamUsedPercent_ >= config_.memory.ramUsedCritPercent) {
    ++critCount_;
    if (log != nullptr) {
      log->error(label(), 0,
                 fmt::format("RAM {:.1f}% >= {:.0f}%", lastRamUsedPercent_,
                             config_.memory.ramUsedCritPercent));
    }
  } else if (lastRamUsedPercent_ >= config_.memory.ramUsedWarnPercent) {
    ++warnCount_;
    if (log != nullptr) {
      log->warning(label(), 0,
                   fmt::format("RAM {:.1f}% >= {:.0f}%", lastRamUsedPercent_,
                               config_.memory.ramUsedWarnPercent));
    }
  }

  if (lastSwapUsedBytes_ >= config_.memory.swapUsedCritBytes) {
    ++critCount_;
    if (log != nullptr) {
      log->error(
          label(), 0,
          fmt::format("swap {}B >= {}B", lastSwapUsedBytes_, config_.memory.swapUsedCritBytes));
    }
  } else if (lastSwapUsedBytes_ >= config_.memory.swapUsedWarnBytes) {
    ++warnCount_;
    if (log != nullptr) {
      log->warning(
          label(), 0,
          fmt::format("swap {}B >= {}B", lastSwapUsedBytes_, config_.memory.swapUsedWarnBytes));
    }
  }

  // File descriptors (NOT RT-safe: iterates /proc/self/fd)
  // Using getFdSoftLimit which IS RT-safe (getrlimit syscall)
  lastFdCount_ = seeker::system::getOpenFdCount();
  const std::uint64_t FD_LIMIT = seeker::system::getFdSoftLimit();
  if (FD_LIMIT > 0) {
    lastFdUsedPercent_ = static_cast<float>(lastFdCount_) / static_cast<float>(FD_LIMIT) * 100.0F;

    if (lastFdUsedPercent_ >= config_.memory.fdUsedCritPercent) {
      ++critCount_;
      if (log != nullptr) {
        log->error(label(), 0,
                   fmt::format("FD {}/{} ({:.1f}%) >= {:.0f}%", lastFdCount_, FD_LIMIT,
                               lastFdUsedPercent_, config_.memory.fdUsedCritPercent));
      }
    } else if (lastFdUsedPercent_ >= config_.memory.fdUsedWarnPercent) {
      ++warnCount_;
      if (log != nullptr) {
        log->warning(label(), 0,
                     fmt::format("FD {}/{} ({:.1f}%) >= {:.0f}%", lastFdCount_, FD_LIMIT,
                                 lastFdUsedPercent_, config_.memory.fdUsedWarnPercent));
      }
    }
  }
}

/* ----------------------------- GPU Sampling ----------------------------- */

void SystemMonitor::sampleGpu() noexcept {
#if COMPAT_NVML_AVAILABLE
  auto* log = componentLog();

  const auto TEL = seeker::gpu::getGpuTelemetry(config_.gpu.deviceIndex);
  lastGpuTempC_ = static_cast<std::int16_t>(TEL.temperatureC);
  lastGpuUtilPercent_ = static_cast<std::int8_t>(TEL.gpuUtilization);
  lastGpuMemUsedPercent_ = static_cast<float>(TEL.memoryUtilization);

  // Temperature
  if (lastGpuTempC_ >= config_.gpu.tempCritC) {
    ++critCount_;
    if (log != nullptr) {
      log->error(label(), 0,
                 fmt::format("GPU temp {}C >= {}C", lastGpuTempC_, config_.gpu.tempCritC));
    }
  } else if (lastGpuTempC_ >= config_.gpu.tempWarnC) {
    ++warnCount_;
    if (log != nullptr) {
      log->warning(label(), 0,
                   fmt::format("GPU temp {}C >= {}C", lastGpuTempC_, config_.gpu.tempWarnC));
    }
  }

  // Compute utilization
  const float GPU_UTIL = static_cast<float>(TEL.gpuUtilization);
  if (GPU_UTIL >= config_.gpu.utilCritPercent) {
    ++critCount_;
    if (log != nullptr) {
      log->error(label(), 0,
                 fmt::format("GPU util {:.0f}% >= {:.0f}%", GPU_UTIL, config_.gpu.utilCritPercent));
    }
  } else if (GPU_UTIL >= config_.gpu.utilWarnPercent) {
    ++warnCount_;
    if (log != nullptr) {
      log->warning(
          label(), 0,
          fmt::format("GPU util {:.0f}% >= {:.0f}%", GPU_UTIL, config_.gpu.utilWarnPercent));
    }
  }

  // GPU memory
  const float GPU_MEM = static_cast<float>(TEL.memoryUtilization);
  if (GPU_MEM >= config_.gpu.memUsedCritPercent) {
    ++critCount_;
    if (log != nullptr) {
      log->error(
          label(), 0,
          fmt::format("GPU mem {:.0f}% >= {:.0f}%", GPU_MEM, config_.gpu.memUsedCritPercent));
    }
  } else if (GPU_MEM >= config_.gpu.memUsedWarnPercent) {
    ++warnCount_;
    if (log != nullptr) {
      log->warning(
          label(), 0,
          fmt::format("GPU mem {:.0f}% >= {:.0f}%", GPU_MEM, config_.gpu.memUsedWarnPercent));
    }
  }

  // Throttle state
  if (config_.gpu.throttleWarnOnAny && TEL.throttleReasons.isThrottling()) {
    ++warnCount_;
    if (log != nullptr) {
      log->warning(label(), 0,
                   fmt::format("GPU throttling (thermal={} power={})",
                               TEL.throttleReasons.isThermalThrottling() ? "yes" : "no",
                               TEL.throttleReasons.isPowerThrottling() ? "yes" : "no"));
    }
  }
#endif
}

/* ----------------------------- Accessors ----------------------------- */

float SystemMonitor::lastCpuLoad(std::size_t monitoredIdx) const noexcept {
  if (monitoredIdx >= MAX_MONITORED_CORES) {
    return 0.0F;
  }
  return lastCoreLoad_[monitoredIdx];
}

} // namespace support
} // namespace system_core
