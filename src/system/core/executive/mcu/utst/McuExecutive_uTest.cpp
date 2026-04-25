/**
 * @file McuExecutive_uTest.cpp
 * @brief Unit tests for McuExecutive.
 */

#include "src/system/core/executive/mcu/inc/McuExecutive.hpp"
#include "src/system/core/executive/mcu/inc/FreeRunningSource.hpp"

#include <gtest/gtest.h>

using executive::RunResult;
using executive::mcu::FreeRunningSource;
using McuExecutive = executive::mcu::McuExecutive<>;
using system_core::scheduler::mcu::McuTaskEntry;

/* ----------------------------- Test Helpers ----------------------------- */

static int g_taskRunCount = 0;

static void testTask(void* /*ctx*/) noexcept { ++g_taskRunCount; }

static void resetTestState() { g_taskRunCount = 0; }

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default construction creates executive with expected state. */
TEST(McuExecutive_DefaultConstruction, InitialState) {
  FreeRunningSource tickSource(100);
  McuExecutive exec(&tickSource, 100);

  EXPECT_EQ(exec.componentId(), 0);
  EXPECT_STREQ(exec.componentName(), "McuExecutive");
  EXPECT_EQ(exec.componentType(), system_core::system_component::ComponentType::EXECUTIVE);
  EXPECT_STREQ(exec.label(), "EXEC_MCU");
  EXPECT_FALSE(exec.isInitialized());
  EXPECT_EQ(exec.cycleCount(), 0);
  EXPECT_FALSE(exec.isShutdownRequested());
}

/* ----------------------------- Lifecycle Tests ----------------------------- */

/** @test init() succeeds with valid dependencies. */
TEST(McuExecutive_Lifecycle, InitSucceeds) {
  FreeRunningSource tickSource(100);
  McuExecutive exec(&tickSource, 100);

  const std::uint8_t RESULT = exec.init();

  EXPECT_EQ(RESULT, 0);
  EXPECT_TRUE(exec.isInitialized());
  EXPECT_EQ(exec.lastError(), nullptr);
}

/** @test init() fails with null tick source. */
TEST(McuExecutive_Lifecycle, InitFailsNullTickSource) {
  McuExecutive exec(nullptr, 100);

  const std::uint8_t RESULT = exec.init();

  EXPECT_NE(RESULT, 0);
  EXPECT_FALSE(exec.isInitialized());
  EXPECT_NE(exec.lastError(), nullptr);
}

/** @test init() is idempotent. */
TEST(McuExecutive_Lifecycle, InitIdempotent) {
  FreeRunningSource tickSource(100);
  McuExecutive exec(&tickSource, 100);
  (void)exec.init();

  const std::uint8_t RESULT = exec.init();

  EXPECT_EQ(RESULT, 0);
}

/** @test reset() clears state and allows reinitialization. */
TEST(McuExecutive_Lifecycle, ResetClearsState) {
  FreeRunningSource tickSource(100);
  McuExecutive exec(&tickSource, 100, 10);
  (void)exec.init();
  (void)exec.run();

  exec.reset();

  EXPECT_FALSE(exec.isInitialized());
  EXPECT_EQ(exec.cycleCount(), 0);
  EXPECT_FALSE(exec.isShutdownRequested());
}

/* ----------------------------- Execution Tests ----------------------------- */

/** @test run() returns ERROR_INIT when not initialized. */
TEST(McuExecutive_Execution, RunFailsIfNotInitialized) {
  FreeRunningSource tickSource(100);
  McuExecutive exec(&tickSource, 100);

  const RunResult RESULT = exec.run();

  EXPECT_EQ(RESULT, RunResult::ERROR_INIT);
}

/** @test run() executes specified number of cycles. */
TEST(McuExecutive_Execution, RunsMaxCycles) {
  resetTestState();
  FreeRunningSource tickSource(100);
  McuExecutive exec(&tickSource, 100, 50);
  exec.addTask({testTask, nullptr, 1, 1, 0, 0, 1});
  (void)exec.init();

  const RunResult RESULT = exec.run();

  EXPECT_EQ(RESULT, RunResult::SUCCESS);
  EXPECT_EQ(exec.cycleCount(), 50);
  EXPECT_EQ(g_taskRunCount, 50);
}

/** @test run() respects shutdown request. */
TEST(McuExecutive_Execution, ShutdownStopsRun) {
  resetTestState();
  FreeRunningSource tickSource(100);

  // Task that requests shutdown after 10 calls
  static int callCount = 0;
  callCount = 0;
  auto shutdownTask = [](void* ctx) noexcept {
    ++callCount;
    if (callCount >= 10) {
      static_cast<McuExecutive*>(ctx)->shutdown();
    }
  };

  McuExecutive exec(&tickSource, 100, 1000);
  exec.addTask({shutdownTask, &exec, 1, 1, 0, 0, 1});
  (void)exec.init();

  const RunResult RESULT = exec.run();

  EXPECT_EQ(RESULT, RunResult::SUCCESS);
  EXPECT_TRUE(exec.isShutdownRequested());
  EXPECT_EQ(callCount, 10);
}

/** @test shutdown() sets flag. */
TEST(McuExecutive_Execution, ShutdownSetsFlag) {
  FreeRunningSource tickSource(100);
  McuExecutive exec(&tickSource, 100);
  (void)exec.init();

  exec.shutdown();

  EXPECT_TRUE(exec.isShutdownRequested());
}

/* ----------------------------- Accessors Tests ----------------------------- */

/** @test tickSource() returns injected tick source. */
TEST(McuExecutive_Accessors, TickSourceAccessor) {
  FreeRunningSource tickSource(100);
  McuExecutive exec(&tickSource, 100);

  EXPECT_EQ(exec.tickSource(), &tickSource);
}

/** @test scheduler() returns owned scheduler reference. */
TEST(McuExecutive_Accessors, SchedulerAccessor) {
  FreeRunningSource tickSource(100);
  McuExecutive exec(&tickSource, 100);

  EXPECT_EQ(exec.scheduler().fundamentalFreq(), 100);
}

/** @test setMaxCycles() updates max cycles. */
TEST(McuExecutive_Accessors, SetMaxCycles) {
  resetTestState();
  FreeRunningSource tickSource(100);
  McuExecutive exec(&tickSource, 100, 100);
  exec.addTask({testTask, nullptr, 1, 1, 0, 0, 1});

  exec.setMaxCycles(25);
  (void)exec.init();
  (void)exec.run();

  EXPECT_EQ(exec.cycleCount(), 25);
}

/* ----------------------------- IExecutive Interface Tests ----------------------------- */

/** @test McuExecutive implements IExecutive interface. */
TEST(McuExecutive_Interface, ImplementsIExecutive) {
  FreeRunningSource tickSource(100);
  McuExecutive exec(&tickSource, 100, 10);
  executive::IExecutive* iface = &exec;

  static_cast<void>(exec.init());
  EXPECT_FALSE(iface->isShutdownRequested());

  const RunResult RESULT = iface->run();
  EXPECT_EQ(RESULT, RunResult::SUCCESS);
  EXPECT_EQ(iface->cycleCount(), 10);

  iface->shutdown();
  EXPECT_TRUE(iface->isShutdownRequested());
}

/* ----------------------------- Fast-Forward Tests ----------------------------- */

/** @test setFastForward() and isFastForward() control the flag. */
TEST(McuExecutive_FastForward, SetsFlag) {
  FreeRunningSource tickSource(100);
  McuExecutive exec(&tickSource, 100);

  EXPECT_FALSE(exec.isFastForward());

  exec.setFastForward(true);
  EXPECT_TRUE(exec.isFastForward());

  exec.setFastForward(false);
  EXPECT_FALSE(exec.isFastForward());
}

/** @test reset() clears fast-forward flag. */
TEST(McuExecutive_FastForward, ResetClearsFastForward) {
  FreeRunningSource tickSource(100);
  McuExecutive exec(&tickSource, 100, 10);
  (void)exec.init();
  exec.setFastForward(true);
  (void)exec.run();

  exec.reset();

  EXPECT_FALSE(exec.isFastForward());
}

/** @test Fast-forward mode executes all cycles. */
TEST(McuExecutive_FastForward, RunsAllCycles) {
  resetTestState();
  FreeRunningSource tickSource(100);
  McuExecutive exec(&tickSource, 100, 200);
  exec.addTask({testTask, nullptr, 1, 1, 0, 0, 1});
  (void)exec.init();
  exec.setFastForward(true);

  const RunResult RESULT = exec.run();

  EXPECT_EQ(RESULT, RunResult::SUCCESS);
  EXPECT_EQ(exec.cycleCount(), 200);
  EXPECT_EQ(g_taskRunCount, 200);
}

/* ----------------------------- Integration Tests ----------------------------- */

/** @test Full integration with scheduler executing multiple tasks. */
TEST(McuExecutive_Integration, MultipleTasksExecution) {
  static int task1Count = 0;
  static int task2Count = 0;
  task1Count = 0;
  task2Count = 0;

  auto task1 = [](void* /*ctx*/) noexcept { ++task1Count; };
  auto task2 = [](void* /*ctx*/) noexcept { ++task2Count; };

  FreeRunningSource tickSource(100);
  McuExecutive exec(&tickSource, 100, 100);
  exec.addTask({task1, nullptr, 1, 1, 0, 0, 1});  // Every tick
  exec.addTask({task2, nullptr, 1, 10, 0, 0, 2}); // Every 10 ticks
  (void)exec.init();

  const RunResult RESULT = exec.run();

  EXPECT_EQ(RESULT, RunResult::SUCCESS);
  EXPECT_EQ(task1Count, 100);
  // Task2 runs at ticks 0, 10, 20, ..., 90 = 10 times
  EXPECT_EQ(task2Count, 10);
}
