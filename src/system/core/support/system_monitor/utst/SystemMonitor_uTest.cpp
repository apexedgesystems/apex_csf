/**
 * @file SystemMonitor_uTest.cpp
 * @brief Unit tests for the SystemMonitor support component.
 *
 * Tests cover:
 *  - Default construction and identity
 *  - Configuration (thresholds, domain enable/disable)
 *  - Init snapshot population
 *  - Telemetry task execution
 *  - Threshold alerting (warn/crit counters)
 *  - Accessor correctness
 *
 * @note These tests run on the host and exercise real /proc and /sys reads.
 *       They verify invariants (e.g., core count > 0) rather than exact values.
 */

#include "src/system/core/support/system_monitor/inc/SystemMonitor.hpp"
#include "src/system/core/support/system_monitor/inc/SystemMonitorConfig.hpp"
#include "src/system/core/support/system_monitor/inc/SystemMonitorSnapshot.hpp"

#include <cstring>

#include <thread>

#include <gtest/gtest.h>

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verify component identity constants. */
TEST(SystemMonitor, ComponentIdentity) {
  system_core::support::SystemMonitor monitor;

  EXPECT_EQ(monitor.componentId(), 200);
  EXPECT_STREQ(monitor.componentName(), "SystemMonitor");
  EXPECT_STREQ(monitor.label(), "SYS_MON");
}

/** @test Verify default config values. */
TEST(SystemMonitor, DefaultConfig) {
  system_core::support::SystemMonitor monitor;
  const auto& CFG = monitor.config();

  EXPECT_FLOAT_EQ(CFG.sampleRateHz, 1.0F);

  // CPU domain
  EXPECT_TRUE(CFG.cpu.enabled);
  EXPECT_FLOAT_EQ(CFG.cpu.loadWarnPercent, 80.0F);
  EXPECT_FLOAT_EQ(CFG.cpu.loadCritPercent, 95.0F);
  EXPECT_EQ(CFG.cpu.tempWarnC, 75);
  EXPECT_EQ(CFG.cpu.tempCritC, 85);
  EXPECT_EQ(CFG.cpu.monitoredCoreCount, 0);

  // Memory domain
  EXPECT_TRUE(CFG.memory.enabled);
  EXPECT_FLOAT_EQ(CFG.memory.ramUsedWarnPercent, 80.0F);
  EXPECT_EQ(CFG.memory.swapUsedWarnBytes, 1U);

  // GPU domain
  EXPECT_FALSE(CFG.gpu.enabled);
}

/** @test Verify initial counters are zero. */
TEST(SystemMonitor, InitialCountersZero) {
  system_core::support::SystemMonitor monitor;

  EXPECT_EQ(monitor.warnCount(), 0U);
  EXPECT_EQ(monitor.critCount(), 0U);
  EXPECT_EQ(monitor.sampleCount(), 0U);
}

/* ----------------------------- Configuration ----------------------------- */

/** @test Custom config overrides defaults. */
TEST(SystemMonitor, SetConfig) {
  system_core::support::SystemMonitor monitor;

  system_core::support::SystemMonitorConfig cfg;
  cfg.sampleRateHz = 10.0F;
  cfg.cpu.loadWarnPercent = 50.0F;
  cfg.cpu.monitoredCores[0] = 0;
  cfg.cpu.monitoredCores[1] = 2;
  cfg.cpu.monitoredCoreCount = 2;
  cfg.memory.enabled = false;
  cfg.gpu.enabled = false;

  monitor.setConfig(cfg);

  EXPECT_FLOAT_EQ(monitor.config().sampleRateHz, 10.0F);
  EXPECT_FLOAT_EQ(monitor.config().cpu.loadWarnPercent, 50.0F);
  EXPECT_EQ(monitor.config().cpu.monitoredCoreCount, 2);
  EXPECT_FALSE(monitor.config().memory.enabled);
}

/* ----------------------------- Init Snapshot ----------------------------- */

/** @test Init populates kernel snapshot with non-empty release string. */
TEST(SystemMonitor, InitKernelSnapshot) {
  system_core::support::SystemMonitor monitor;
  monitor.setInstanceIndex(0);
  (void)monitor.init();

  const auto& SNAP = monitor.snapshot();

  // Kernel release should be non-empty on any Linux host
  EXPECT_GT(std::strlen(SNAP.kernel.release.data()), 0U);
}

/** @test Init populates CPU snapshot with valid core count. */
TEST(SystemMonitor, InitCpuSnapshot) {
  system_core::support::SystemMonitor monitor;
  monitor.setInstanceIndex(0);
  (void)monitor.init();

  const auto& SNAP = monitor.snapshot();

  // Must have at least 1 core
  EXPECT_GE(SNAP.cpu.coreCount, 1);

  // Clock source should be non-empty
  EXPECT_GT(std::strlen(SNAP.cpu.clockSource.data()), 0U);
}

/** @test Init populates memory snapshot with valid RAM total. */
TEST(SystemMonitor, InitMemorySnapshot) {
  system_core::support::SystemMonitor monitor;
  monitor.setInstanceIndex(0);
  (void)monitor.init();

  const auto& SNAP = monitor.snapshot();

  // Must have some RAM
  EXPECT_GT(SNAP.memory.totalRamBytes, 0U);

  // FD limits should be reasonable
  EXPECT_GT(SNAP.memory.fdSoftLimit, 0U);
}

/** @test Init populates GPU snapshot as unavailable when GPU disabled. */
TEST(SystemMonitor, InitGpuSnapshotDisabled) {
  system_core::support::SystemMonitor monitor;
  monitor.setInstanceIndex(0);

  system_core::support::SystemMonitorConfig cfg;
  cfg.gpu.enabled = false;
  monitor.setConfig(cfg);
  (void)monitor.init();

  EXPECT_FALSE(monitor.snapshot().gpu.available);
}

/* ----------------------------- Telemetry Task ----------------------------- */

/** @test Telemetry task runs and increments sample counter. */
TEST(SystemMonitor, TelemetryIncrementsSampleCount) {
  system_core::support::SystemMonitor monitor;
  monitor.setInstanceIndex(0);

  system_core::support::SystemMonitorConfig cfg;
  cfg.cpu.monitoredCores[0] = 0;
  cfg.cpu.monitoredCoreCount = 1;
  cfg.gpu.enabled = false;
  monitor.setConfig(cfg);
  (void)monitor.init();

  EXPECT_EQ(monitor.sampleCount(), 0U);

  // Need a small delay so the CPU utilization delta is non-zero
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  monitor.telemetry();
  EXPECT_EQ(monitor.sampleCount(), 1U);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  monitor.telemetry();
  EXPECT_EQ(monitor.sampleCount(), 2U);
}

/** @test Telemetry populates CPU load values. */
TEST(SystemMonitor, TelemetryCpuLoad) {
  system_core::support::SystemMonitor monitor;
  monitor.setInstanceIndex(0);

  system_core::support::SystemMonitorConfig cfg;
  cfg.cpu.monitoredCores[0] = 0;
  cfg.cpu.monitoredCoreCount = 1;
  cfg.gpu.enabled = false;
  monitor.setConfig(cfg);
  (void)monitor.init();

  // Wait for measurable delta
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  monitor.telemetry();

  // CPU load should be between 0 and 100
  const float LOAD = monitor.lastCpuLoad(0);
  EXPECT_GE(LOAD, 0.0F);
  EXPECT_LE(LOAD, 100.0F);
}

/** @test Telemetry populates RAM utilization. */
TEST(SystemMonitor, TelemetryMemory) {
  system_core::support::SystemMonitor monitor;
  monitor.setInstanceIndex(0);

  system_core::support::SystemMonitorConfig cfg;
  cfg.cpu.enabled = false;
  cfg.gpu.enabled = false;
  monitor.setConfig(cfg);
  (void)monitor.init();

  monitor.telemetry();

  // RAM utilization should be between 0 and 100
  EXPECT_GE(monitor.lastRamUsedPercent(), 0.0F);
  EXPECT_LE(monitor.lastRamUsedPercent(), 100.0F);
}

/** @test Telemetry populates FD count. */
TEST(SystemMonitor, TelemetryFdCount) {
  system_core::support::SystemMonitor monitor;
  monitor.setInstanceIndex(0);

  system_core::support::SystemMonitorConfig cfg;
  cfg.cpu.enabled = false;
  cfg.gpu.enabled = false;
  monitor.setConfig(cfg);
  (void)monitor.init();

  monitor.telemetry();

  // Must have at least a few open FDs (stdin, stdout, stderr)
  EXPECT_GE(monitor.lastFdCount(), 3U);
}

/** @test CPU temperature returns a value (may be -1 in containers). */
TEST(SystemMonitor, TelemetryCpuTemp) {
  system_core::support::SystemMonitor monitor;
  monitor.setInstanceIndex(0);

  system_core::support::SystemMonitorConfig cfg;
  cfg.gpu.enabled = false;
  cfg.cpu.monitoredCores[0] = 0;
  cfg.cpu.monitoredCoreCount = 1;
  monitor.setConfig(cfg);
  (void)monitor.init();

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  monitor.telemetry();

  // Temperature is either a reasonable value or -1 (unavailable in Docker)
  const std::int16_t TEMP = monitor.lastCpuTempC();
  EXPECT_TRUE(TEMP == -1 || (TEMP >= -40 && TEMP <= 150));
}

/* ----------------------------- Threshold Alerting ----------------------------- */

/** @test Warn threshold triggers on high RAM usage. */
TEST(SystemMonitor, WarnThresholdRam) {
  system_core::support::SystemMonitor monitor;
  monitor.setInstanceIndex(0);

  // Set very low threshold so it always triggers
  system_core::support::SystemMonitorConfig cfg;
  cfg.cpu.enabled = false;
  cfg.gpu.enabled = false;
  cfg.memory.ramUsedWarnPercent = 0.001F;    // 0.001% - will always trigger
  cfg.memory.ramUsedCritPercent = 99.99F;    // High crit so only warn fires
  cfg.memory.swapUsedWarnBytes = UINT64_MAX; // Disable swap warn
  cfg.memory.swapUsedCritBytes = UINT64_MAX;
  cfg.memory.fdUsedWarnPercent = 100.0F; // Disable FD warn
  cfg.memory.fdUsedCritPercent = 100.0F;
  monitor.setConfig(cfg);
  (void)monitor.init();

  monitor.telemetry();

  EXPECT_GE(monitor.warnCount(), 1U);
}

/** @test Crit threshold triggers on high RAM usage. */
TEST(SystemMonitor, CritThresholdRam) {
  system_core::support::SystemMonitor monitor;
  monitor.setInstanceIndex(0);

  // Set very low crit threshold
  system_core::support::SystemMonitorConfig cfg;
  cfg.cpu.enabled = false;
  cfg.gpu.enabled = false;
  cfg.memory.ramUsedWarnPercent = 0.0001F;
  cfg.memory.ramUsedCritPercent = 0.001F; // Will always trigger
  cfg.memory.swapUsedWarnBytes = UINT64_MAX;
  cfg.memory.swapUsedCritBytes = UINT64_MAX;
  cfg.memory.fdUsedWarnPercent = 100.0F;
  cfg.memory.fdUsedCritPercent = 100.0F;
  monitor.setConfig(cfg);
  (void)monitor.init();

  monitor.telemetry();

  EXPECT_GE(monitor.critCount(), 1U);
}

/** @test No alerts when thresholds are set very high. */
TEST(SystemMonitor, NoAlertsWithHighThresholds) {
  system_core::support::SystemMonitor monitor;
  monitor.setInstanceIndex(0);

  system_core::support::SystemMonitorConfig cfg;
  cfg.cpu.enabled = false;
  cfg.gpu.enabled = false;
  cfg.memory.ramUsedWarnPercent = 100.0F;
  cfg.memory.ramUsedCritPercent = 100.0F;
  cfg.memory.swapUsedWarnBytes = UINT64_MAX;
  cfg.memory.swapUsedCritBytes = UINT64_MAX;
  cfg.memory.fdUsedWarnPercent = 100.0F;
  cfg.memory.fdUsedCritPercent = 100.0F;
  monitor.setConfig(cfg);
  (void)monitor.init();

  monitor.telemetry();

  EXPECT_EQ(monitor.warnCount(), 0U);
  EXPECT_EQ(monitor.critCount(), 0U);
}

/* ----------------------------- Accessor Tests ----------------------------- */

/** @test lastCpuLoad out of bounds returns 0. */
TEST(SystemMonitor, CpuLoadOutOfBounds) {
  system_core::support::SystemMonitor monitor;

  EXPECT_FLOAT_EQ(monitor.lastCpuLoad(999), 0.0F);
  EXPECT_FLOAT_EQ(monitor.lastCpuLoad(system_core::support::MAX_MONITORED_CORES), 0.0F);
}

/** @test Disabled domains produce no alerts and no side effects. */
TEST(SystemMonitor, DisabledDomainsNoEffect) {
  system_core::support::SystemMonitor monitor;
  monitor.setInstanceIndex(0);

  system_core::support::SystemMonitorConfig cfg;
  cfg.cpu.enabled = false;
  cfg.memory.enabled = false;
  cfg.gpu.enabled = false;
  monitor.setConfig(cfg);
  (void)monitor.init();

  monitor.telemetry();
  monitor.telemetry();
  monitor.telemetry();

  EXPECT_EQ(monitor.sampleCount(), 3U);
  EXPECT_EQ(monitor.warnCount(), 0U);
  EXPECT_EQ(monitor.critCount(), 0U);
}

/* ----------------------------- Config Struct Tests ----------------------------- */

/** @test CpuThresholds default values. */
TEST(SystemMonitorConfig, CpuDefaults) {
  system_core::support::CpuThresholds cpu;

  EXPECT_TRUE(cpu.enabled);
  EXPECT_EQ(cpu.monitoredCoreCount, 0);
  EXPECT_FLOAT_EQ(cpu.loadWarnPercent, 80.0F);
  EXPECT_FLOAT_EQ(cpu.loadCritPercent, 95.0F);
  EXPECT_EQ(cpu.tempWarnC, 75);
  EXPECT_EQ(cpu.tempCritC, 85);
  EXPECT_EQ(cpu.rtThrottleWarn, 1U);
  EXPECT_EQ(cpu.rtThrottleCrit, 10U);
}

/** @test MemoryThresholds default values. */
TEST(SystemMonitorConfig, MemoryDefaults) {
  system_core::support::MemoryThresholds mem;

  EXPECT_TRUE(mem.enabled);
  EXPECT_FLOAT_EQ(mem.ramUsedWarnPercent, 80.0F);
  EXPECT_FLOAT_EQ(mem.ramUsedCritPercent, 95.0F);
  EXPECT_EQ(mem.swapUsedWarnBytes, 1U);
  EXPECT_EQ(mem.fdUsedWarnPercent, 70.0F);
}

/** @test GpuThresholds default values. */
TEST(SystemMonitorConfig, GpuDefaults) {
  system_core::support::GpuThresholds gpu;

  EXPECT_FALSE(gpu.enabled);
  EXPECT_EQ(gpu.deviceIndex, 0);
  EXPECT_EQ(gpu.tempWarnC, 75);
  EXPECT_FLOAT_EQ(gpu.utilWarnPercent, 90.0F);
  EXPECT_TRUE(gpu.throttleWarnOnAny);
}

/* ----------------------------- Snapshot Struct Tests ----------------------------- */

/** @test Default snapshot has zeroed fields. */
TEST(SystemMonitorSnapshot, DefaultConstruction) {
  system_core::support::SystemMonitorSnapshot snap;

  EXPECT_EQ(snap.kernel.release[0], '\0');
  EXPECT_FALSE(snap.kernel.isRtKernel);
  EXPECT_EQ(snap.cpu.coreCount, 0);
  EXPECT_EQ(snap.memory.totalRamBytes, 0U);
  EXPECT_FALSE(snap.gpu.available);
}
