/**
 * @file SchedulableTask_uTest.cpp
 * @brief Unit tests for SchedulableTask - minimal task implementation.
 *
 * Coverage:
 *  - Construction with delegate and label
 *  - execute() returns callable result
 *  - getLabel() returns correct label
 *  - callable() accessor for scheduler optimization
 *  - Multiple execute calls work correctly
 *
 * Note: Frequency, priority, affinity are managed by scheduler via TaskConfig.
 * See scheduler tests for TaskConfig and TaskEntry tests.
 */

#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <gtest/gtest.h>
#include <atomic>
#include <cstdint>
#include <string_view>

using apex::concurrency::DelegateU8;
using system_core::schedulable::SchedulableTask;

namespace {

std::uint8_t successTask(void*) noexcept { return 0; }
std::uint8_t testValueTask(void*) noexcept { return 42; }
std::uint8_t errorTask(void*) noexcept { return 255; }

struct Counter {
  std::atomic<int> value{0};
};

std::uint8_t incrementTask(void* ctx) noexcept {
  auto* c = static_cast<Counter*>(ctx);
  return static_cast<std::uint8_t>(c->value.fetch_add(1) + 1);
}

} // namespace

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verifies construction with delegate and label. */
TEST(SchedulableTaskTest, ConstructorSetsLabel) {
  DelegateU8 del{&successTask, nullptr};
  SchedulableTask task(del, "TestLabel");

  EXPECT_EQ(task.getLabel(), "TestLabel");
}

/** @test Verifies empty label is valid. */
TEST(SchedulableTaskTest, ConstructorWithEmptyLabel) {
  DelegateU8 del{&successTask, nullptr};
  SchedulableTask task(del, "");

  EXPECT_TRUE(task.getLabel().empty());
}

/** @test Verifies long label works. */
TEST(SchedulableTaskTest, ConstructorWithLongLabel) {
  DelegateU8 del{&successTask, nullptr};
  const std::string_view LONG_LABEL = "this_is_a_very_long_task_label_for_testing_purposes";
  SchedulableTask task(del, LONG_LABEL);

  EXPECT_EQ(task.getLabel(), LONG_LABEL);
}

/* ----------------------------- SchedulableTask Method Tests ----------------------------- */

/** @test Verifies execute returns task result (success). */
TEST(SchedulableTaskTest, ExecuteReturnsSuccess) {
  DelegateU8 del{&successTask, nullptr};
  SchedulableTask task(del, "success");

  EXPECT_EQ(task.execute(), 0);
}

/** @test Verifies execute returns task result (specific value). */
TEST(SchedulableTaskTest, ExecuteReturnsSpecificValue) {
  DelegateU8 del{&testValueTask, nullptr};
  SchedulableTask task(del, "value42");

  EXPECT_EQ(task.execute(), 42);
}

/** @test Verifies execute returns error code. */
TEST(SchedulableTaskTest, ExecuteReturnsErrorCode) {
  DelegateU8 del{&errorTask, nullptr};
  SchedulableTask task(del, "error");

  EXPECT_EQ(task.execute(), 255);
}

/** @test Verifies multiple execute calls work correctly. */
TEST(SchedulableTaskTest, ExecuteMultipleTimes) {
  Counter counter;
  DelegateU8 del{&incrementTask, &counter};
  SchedulableTask task(del, "counter");

  EXPECT_EQ(task.execute(), 1);
  EXPECT_EQ(task.execute(), 2);
  EXPECT_EQ(task.execute(), 3);
  EXPECT_EQ(counter.value.load(), 3);
}

/** @test Verifies execute is deterministic. */
TEST(SchedulableTaskTest, ExecuteIsDeterministic) {
  DelegateU8 del{&testValueTask, nullptr};
  SchedulableTask task(del, "deterministic");

  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(task.execute(), 42);
  }
}

/* ----------------------------- API Tests ----------------------------- */

/** @test Verifies callable() returns working delegate. */
TEST(SchedulableTaskTest, CallableAccessorReturnsDelegate) {
  DelegateU8 del{&testValueTask, nullptr};
  SchedulableTask task(del, "test");

  const auto& callable = task.callable();
  EXPECT_EQ(callable(), 42);
}

/** @test Verifies callable and execute return same result. */
TEST(SchedulableTaskTest, CallableMatchesExecute) {
  DelegateU8 del{&testValueTask, nullptr};
  SchedulableTask task(del, "test");

  EXPECT_EQ(task.callable()(), task.execute());
}

/** @test Verifies callable works with context. */
TEST(SchedulableTaskTest, CallableWithContext) {
  Counter counter;
  DelegateU8 del{&incrementTask, &counter};
  SchedulableTask task(del, "ctx");

  const auto& callable = task.callable();
  EXPECT_EQ(callable(), 1);
  EXPECT_EQ(callable(), 2);
  EXPECT_EQ(counter.value.load(), 2);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test Verifies multiple tasks are independent. */
TEST(SchedulableTaskTest, MultipleTasksIndependent) {
  Counter c1, c2;
  DelegateU8 del1{&incrementTask, &c1};
  DelegateU8 del2{&incrementTask, &c2};

  SchedulableTask task1(del1, "task1");
  SchedulableTask task2(del2, "task2");

  EXPECT_EQ(task1.execute(), 1);
  EXPECT_EQ(task1.execute(), 2);
  EXPECT_EQ(task2.execute(), 1);

  EXPECT_EQ(c1.value.load(), 2);
  EXPECT_EQ(c2.value.load(), 1);
}

/** @test Verifies tasks with same delegate are independent. */
TEST(SchedulableTaskTest, SameDelegateMultipleTasks) {
  Counter counter;
  DelegateU8 del{&incrementTask, &counter};

  SchedulableTask task1(del, "task1");
  SchedulableTask task2(del, "task2");

  // Both tasks share the same counter context
  EXPECT_EQ(task1.execute(), 1);
  EXPECT_EQ(task2.execute(), 2);
  EXPECT_EQ(task1.execute(), 3);
  EXPECT_EQ(counter.value.load(), 3);
}

/* ----------------------------- Constants Tests ----------------------------- */

/** @test Verifies task size remains reasonable for RT systems. */
TEST(SchedulableTaskTest, MinimalMemoryFootprint) {
  // SchedulableTask layout:
  // vtable ptr: 8 bytes
  // DelegateU8: 16 bytes (fnptr + void*)
  // string_view: 16 bytes (ptr + size)
  // shared_ptr<atomic<int>>: 16 bytes (ptr + control block)
  // expectedPhase_: 4 bytes
  // maxPhase_: 4 bytes
  // sequencingEnabled_: 1 byte + 7 padding
  // Total expected: ~72 bytes
  constexpr std::size_t MAX_EXPECTED_SIZE = 80;
  EXPECT_LE(sizeof(SchedulableTask), MAX_EXPECTED_SIZE);
}
