/**
 * @file SchedulerLite_uTest.cpp
 * @brief Unit tests for SchedulerLite.
 */

#include "src/system/core/components/scheduler/mcu/inc/SchedulerLite.hpp"

#include <gtest/gtest.h>

using system_core::scheduler::mcu::LiteTaskEntry;
using SchedulerLite = system_core::scheduler::mcu::SchedulerLite<>;

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
TEST(SchedulerLite_DefaultConstruction, InitialState) {
  SchedulerLite sched;

  EXPECT_EQ(sched.fundamentalFreq(), 100);
  EXPECT_EQ(sched.taskCount(), 0);
  EXPECT_EQ(sched.tickCount(), 0);
  EXPECT_FALSE(sched.isInitialized());
  EXPECT_EQ(sched.componentId(), 1);
  EXPECT_STREQ(sched.componentName(), "SchedulerLite");
}

/** @test Custom frequency is applied. */
TEST(SchedulerLite_DefaultConstruction, CustomFrequency) {
  SchedulerLite sched(200);

  EXPECT_EQ(sched.fundamentalFreq(), 200);
}

/* ----------------------------- Task Registration ----------------------------- */

/** @test addTask() adds task and increments count. */
TEST(SchedulerLite_TaskRegistration, AddSingleTask) {
  SchedulerLite sched;

  LiteTaskEntry entry{testTask, nullptr, 1, 1, 0, 0, 1};
  const bool RESULT = sched.addTask(entry);

  EXPECT_TRUE(RESULT);
  EXPECT_EQ(sched.taskCount(), 1);
}

/** @test addTask() rejects null function pointer. */
TEST(SchedulerLite_TaskRegistration, RejectsNullFunction) {
  SchedulerLite sched;

  LiteTaskEntry entry{nullptr, nullptr, 1, 1, 0, 0, 1};
  const bool RESULT = sched.addTask(entry);

  EXPECT_FALSE(RESULT);
  EXPECT_EQ(sched.taskCount(), 0);
}

/** @test addTask() rejects when table is full. */
TEST(SchedulerLite_TaskRegistration, RejectsWhenFull) {
  SchedulerLite sched;

  // Fill up the task table
  for (std::size_t i = 0; i < SchedulerLite::MAX_TASKS; ++i) {
    LiteTaskEntry entry{testTask, nullptr, 1, 1, 0, 0, static_cast<std::uint8_t>(i)};
    EXPECT_TRUE(sched.addTask(entry));
  }

  // One more should fail
  LiteTaskEntry extra{testTask, nullptr, 1, 1, 0, 0, 99};
  EXPECT_FALSE(sched.addTask(extra));
  EXPECT_EQ(sched.taskCount(), SchedulerLite::MAX_TASKS);
}

/** @test clearTasks() removes all tasks. */
TEST(SchedulerLite_TaskRegistration, ClearTasks) {
  SchedulerLite sched;
  sched.addTask({testTask, nullptr, 1, 1, 0, 0, 1});
  sched.addTask({testTask, nullptr, 1, 1, 0, 0, 2});

  sched.clearTasks();

  EXPECT_EQ(sched.taskCount(), 0);
}

/** @test task() returns entry by index. */
TEST(SchedulerLite_TaskRegistration, TaskByIndex) {
  SchedulerLite sched;
  int context = 42;
  sched.addTask({testTask, &context, 1, 1, 0, 5, 1});

  const LiteTaskEntry* entry = sched.task(0);

  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->fn, testTask);
  EXPECT_EQ(entry->context, &context);
  EXPECT_EQ(entry->priority, 5);
  EXPECT_EQ(entry->taskId, 1);
}

/** @test task() returns nullptr for out-of-range index. */
TEST(SchedulerLite_TaskRegistration, TaskOutOfRange) {
  SchedulerLite sched;

  const LiteTaskEntry* entry = sched.task(0);

  EXPECT_EQ(entry, nullptr);
}

/* ----------------------------- Lifecycle ----------------------------- */

/** @test init() succeeds and sets initialized flag. */
TEST(SchedulerLite_Lifecycle, InitSucceeds) {
  SchedulerLite sched;

  const std::uint8_t RESULT = sched.init();

  EXPECT_EQ(RESULT, 0);
  EXPECT_TRUE(sched.isInitialized());
}

/** @test init() is idempotent. */
TEST(SchedulerLite_Lifecycle, InitIdempotent) {
  SchedulerLite sched;
  (void)sched.init();

  const std::uint8_t RESULT = sched.init();

  EXPECT_EQ(RESULT, 0);
}

/** @test reset() clears tick count. */
TEST(SchedulerLite_Lifecycle, ResetClearsTickCount) {
  SchedulerLite sched;
  (void)sched.init();
  sched.tick();
  sched.tick();

  sched.reset();

  EXPECT_EQ(sched.tickCount(), 0);
  EXPECT_FALSE(sched.isInitialized());
}

/* ----------------------------- Task Execution ----------------------------- */

/** @test tick() executes task and increments tick count. */
TEST(SchedulerLite_Execution, TickExecutesTask) {
  resetTestState();
  SchedulerLite sched;
  int context = 123;
  sched.addTask({testTask, &context, 1, 1, 0, 0, 1});
  (void)sched.init();

  sched.tick();

  EXPECT_EQ(g_taskCallCount, 1);
  EXPECT_EQ(g_lastContext, &context);
  EXPECT_EQ(sched.tickCount(), 1);
}

/** @test Multiple ticks execute task multiple times. */
TEST(SchedulerLite_Execution, MultipleTicksExecuteMultipleTimes) {
  resetTestState();
  SchedulerLite sched;
  sched.addTask({testTask, nullptr, 1, 1, 0, 0, 1});
  (void)sched.init();

  sched.tick();
  sched.tick();
  sched.tick();

  EXPECT_EQ(g_taskCallCount, 3);
  EXPECT_EQ(sched.tickCount(), 3);
}

/** @test Task with lower frequency runs less often. */
TEST(SchedulerLite_Execution, FrequencyDivisor) {
  resetTestState();
  SchedulerLite sched(100); // 100 Hz fundamental
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
TEST(SchedulerLite_Execution, OffsetDelaysExecution) {
  resetTestState();
  SchedulerLite sched(100);
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
TEST(SchedulerLite_Execution, PriorityOrdering) {
  static int order[3];
  static int orderIndex = 0;
  orderIndex = 0;

  auto task1 = [](void* ctx) noexcept { order[orderIndex++] = *static_cast<int*>(ctx); };
  auto task2 = [](void* ctx) noexcept { order[orderIndex++] = *static_cast<int*>(ctx); };
  auto task3 = [](void* ctx) noexcept { order[orderIndex++] = *static_cast<int*>(ctx); };

  int id1 = 1, id2 = 2, id3 = 3;
  SchedulerLite sched;
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

/** @test SchedulerLite implements IScheduler interface. */
TEST(SchedulerLite_Interface, ImplementsIScheduler) {
  SchedulerLite sched(50);
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
TEST(SchedulerLite_Registration, SetInstanceIndex) {
  SchedulerLite sched;

  sched.setInstanceIndex(2);

  EXPECT_TRUE(sched.isRegistered());
  EXPECT_EQ(sched.instanceIndex(), 2);
  // fullUid = (componentId << 8) | instanceIndex = (1 << 8) | 2 = 258
  EXPECT_EQ(sched.fullUid(), (1U << 8) | 2U);
}
