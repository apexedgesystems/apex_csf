/**
 * @file McuScheduler_uTest.cpp
 * @brief Unit tests for McuScheduler.
 */

#include "src/system/core/components/scheduler/mcu/inc/McuScheduler.hpp"

#include <gtest/gtest.h>

using system_core::scheduler::mcu::McuTaskEntry;
using McuScheduler = system_core::scheduler::mcu::McuScheduler<>;

/* ----------------------------- Test Helpers ----------------------------- */

static int g_taskCallCount = 0;
static void* g_lastContext = nullptr;

static void testTask(void* ctx) noexcept {
  ++g_taskCallCount;
  g_lastContext = ctx;
}

static void resetTestState() {
  g_taskCallCount = 0;
  g_lastContext = nullptr;
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default construction creates scheduler with expected state. */
TEST(McuScheduler_DefaultConstruction, InitialState) {
  McuScheduler sched;

  EXPECT_EQ(sched.fundamentalFreq(), 100);
  EXPECT_EQ(sched.taskCount(), 0);
  EXPECT_EQ(sched.tickCount(), 0);
  EXPECT_FALSE(sched.isInitialized());
  EXPECT_EQ(sched.componentId(), 1);
  EXPECT_STREQ(sched.componentName(), "McuScheduler");
}

/** @test Custom frequency is applied. */
TEST(McuScheduler_DefaultConstruction, CustomFrequency) {
  McuScheduler sched(200);

  EXPECT_EQ(sched.fundamentalFreq(), 200);
}

/* ----------------------------- Task Registration ----------------------------- */

/** @test addTask() adds task and increments count. */
TEST(McuScheduler_TaskRegistration, AddSingleTask) {
  McuScheduler sched;

  McuTaskEntry entry{testTask, nullptr, 1, 1, 0, 0, 1};
  const bool RESULT = sched.addTask(entry);

  EXPECT_TRUE(RESULT);
  EXPECT_EQ(sched.taskCount(), 1);
}

/** @test addTask() rejects null function pointer. */
TEST(McuScheduler_TaskRegistration, RejectsNullFunction) {
  McuScheduler sched;

  McuTaskEntry entry{nullptr, nullptr, 1, 1, 0, 0, 1};
  const bool RESULT = sched.addTask(entry);

  EXPECT_FALSE(RESULT);
  EXPECT_EQ(sched.taskCount(), 0);
}

/** @test addTask() rejects when table is full. */
TEST(McuScheduler_TaskRegistration, RejectsWhenFull) {
  McuScheduler sched;

  // Fill up the task table
  for (std::size_t i = 0; i < McuScheduler::MAX_TASKS; ++i) {
    McuTaskEntry entry{testTask, nullptr, 1, 1, 0, 0, static_cast<std::uint8_t>(i)};
    EXPECT_TRUE(sched.addTask(entry));
  }

  // One more should fail
  McuTaskEntry extra{testTask, nullptr, 1, 1, 0, 0, 99};
  EXPECT_FALSE(sched.addTask(extra));
  EXPECT_EQ(sched.taskCount(), McuScheduler::MAX_TASKS);
}

/** @test clearTasks() removes all tasks. */
TEST(McuScheduler_TaskRegistration, ClearTasks) {
  McuScheduler sched;
  sched.addTask({testTask, nullptr, 1, 1, 0, 0, 1});
  sched.addTask({testTask, nullptr, 1, 1, 0, 0, 2});

  sched.clearTasks();

  EXPECT_EQ(sched.taskCount(), 0);
}

/** @test task() returns entry by index. */
TEST(McuScheduler_TaskRegistration, TaskByIndex) {
  McuScheduler sched;
  int context = 42;
  sched.addTask({testTask, &context, 1, 1, 0, 5, 1});

  const McuTaskEntry* entry = sched.task(0);

  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->fn, testTask);
  EXPECT_EQ(entry->context, &context);
  EXPECT_EQ(entry->priority, 5);
  EXPECT_EQ(entry->taskId, 1);
}

/** @test task() returns nullptr for out-of-range index. */
TEST(McuScheduler_TaskRegistration, TaskOutOfRange) {
  McuScheduler sched;

  const McuTaskEntry* entry = sched.task(0);

  EXPECT_EQ(entry, nullptr);
}

/* ----------------------------- Lifecycle ----------------------------- */

/** @test init() succeeds and sets initialized flag. */
TEST(McuScheduler_Lifecycle, InitSucceeds) {
  McuScheduler sched;

  const std::uint8_t RESULT = sched.init();

  EXPECT_EQ(RESULT, 0);
  EXPECT_TRUE(sched.isInitialized());
}

/** @test init() is idempotent. */
TEST(McuScheduler_Lifecycle, InitIdempotent) {
  McuScheduler sched;
  (void)sched.init();

  const std::uint8_t RESULT = sched.init();

  EXPECT_EQ(RESULT, 0);
}

/** @test reset() clears tick count. */
TEST(McuScheduler_Lifecycle, ResetClearsTickCount) {
  McuScheduler sched;
  (void)sched.init();
  sched.tick();
  sched.tick();

  sched.reset();

  EXPECT_EQ(sched.tickCount(), 0);
  EXPECT_FALSE(sched.isInitialized());
}

/* ----------------------------- Task Execution ----------------------------- */

/** @test tick() executes task and increments tick count. */
TEST(McuScheduler_Execution, TickExecutesTask) {
  resetTestState();
  McuScheduler sched;
  int context = 123;
  sched.addTask({testTask, &context, 1, 1, 0, 0, 1});
  (void)sched.init();

  sched.tick();

  EXPECT_EQ(g_taskCallCount, 1);
  EXPECT_EQ(g_lastContext, &context);
  EXPECT_EQ(sched.tickCount(), 1);
}

/** @test Multiple ticks execute task multiple times. */
TEST(McuScheduler_Execution, MultipleTicksExecuteMultipleTimes) {
  resetTestState();
  McuScheduler sched;
  sched.addTask({testTask, nullptr, 1, 1, 0, 0, 1});
  (void)sched.init();

  sched.tick();
  sched.tick();
  sched.tick();

  EXPECT_EQ(g_taskCallCount, 3);
  EXPECT_EQ(sched.tickCount(), 3);
}

/** @test Task with lower frequency runs less often. */
TEST(McuScheduler_Execution, FrequencyDivisor) {
  resetTestState();
  McuScheduler sched(100); // 100 Hz fundamental
  // Task runs at 1/10 fundamental = 10 Hz (every 10 ticks)
  sched.addTask({testTask, nullptr, 1, 10, 0, 0, 1});
  (void)sched.init();

  // Run 20 ticks at 100 Hz
  for (int i = 0; i < 20; ++i) {
    sched.tick();
  }

  // Should run at ticks 0, 10 = 2 times
  EXPECT_EQ(g_taskCallCount, 2);
}

/** @test Task offset delays first execution. */
TEST(McuScheduler_Execution, OffsetDelaysExecution) {
  resetTestState();
  McuScheduler sched(100);
  // Task runs every tick but offset by 5
  sched.addTask({testTask, nullptr, 1, 1, 5, 0, 1});
  (void)sched.init();

  // First 5 ticks - no execution
  for (int i = 0; i < 5; ++i) {
    sched.tick();
  }
  EXPECT_EQ(g_taskCallCount, 0);

  // Tick 5 - first execution
  sched.tick();
  EXPECT_EQ(g_taskCallCount, 1);
}

/** @test Multiple tasks execute in priority order. */
TEST(McuScheduler_Execution, PriorityOrdering) {
  static int order[3];
  static int orderIndex = 0;
  orderIndex = 0;

  auto task1 = [](void* ctx) noexcept { order[orderIndex++] = *static_cast<int*>(ctx); };
  auto task2 = [](void* ctx) noexcept { order[orderIndex++] = *static_cast<int*>(ctx); };
  auto task3 = [](void* ctx) noexcept { order[orderIndex++] = *static_cast<int*>(ctx); };

  int id1 = 1, id2 = 2, id3 = 3;
  McuScheduler sched;
  // Add in reverse priority order
  sched.addTask({task1, &id1, 1, 1, 0, -10, 1}); // Low priority
  sched.addTask({task2, &id2, 1, 1, 0, 10, 2});  // High priority
  sched.addTask({task3, &id3, 1, 1, 0, 0, 3});   // Medium priority
  (void)sched.init();

  sched.tick();

  // Should execute in priority order: task2 (10), task3 (0), task1 (-10)
  EXPECT_EQ(order[0], 2);
  EXPECT_EQ(order[1], 3);
  EXPECT_EQ(order[2], 1);
}

/* ----------------------------- IScheduler Interface ----------------------------- */

/** @test McuScheduler implements IScheduler interface. */
TEST(McuScheduler_Interface, ImplementsIScheduler) {
  McuScheduler sched(50);
  system_core::scheduler::IScheduler* iface = &sched;

  EXPECT_EQ(iface->fundamentalFreq(), 50);
  EXPECT_EQ(iface->taskCount(), 0);
  EXPECT_EQ(iface->tickCount(), 0);

  static_cast<void>(sched.init());
  iface->tick();
  EXPECT_EQ(iface->tickCount(), 1);
}

/* ----------------------------- Registration ----------------------------- */

/** @test setInstanceIndex() computes fullUid correctly. */
TEST(McuScheduler_Registration, SetInstanceIndex) {
  McuScheduler sched;

  sched.setInstanceIndex(2);

  EXPECT_TRUE(sched.isRegistered());
  EXPECT_EQ(sched.instanceIndex(), 2);
  // fullUid = (componentId << 8) | instanceIndex = (1 << 8) | 2 = 258
  EXPECT_EQ(sched.fullUid(), (1U << 8) | 2U);
}
