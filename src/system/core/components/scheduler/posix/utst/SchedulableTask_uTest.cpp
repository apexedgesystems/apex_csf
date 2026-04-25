/**
 * @file SchedulableTask_uTest.cpp
 * @brief Unit tests for SchedulableTask functionality.
 *
 * Coverage:
 *  - Construction with delegate and label
 *  - execute() returns callable result
 *  - getLabel() returns correct label
 *  - callable() accessor
 *  - Integration with TaskBuilder binding helpers
 *
 * Note: Frequency, priority, affinity, and dependencies are now managed
 * by the scheduler via TaskConfig. See TaskConfig_uTest.cpp for those tests.
 */

#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/system/core/infrastructure/schedulable/inc/TaskBuilder.hpp"

#include <cstdint>

#include <atomic>
#include <string>

#include <gtest/gtest.h>

using system_core::schedulable::bindFreeFunction;
using system_core::schedulable::bindLambda;
using system_core::schedulable::bindMember;
using system_core::schedulable::SchedulableTask;

namespace {

std::uint8_t successTask() { return 0; }
std::uint8_t testValueTask() { return 42; }
std::uint8_t errorTask() { return 255; }

struct TestObject {
  std::atomic<int> counter{0};

  std::uint8_t increment() {
    counter.fetch_add(1);
    return static_cast<std::uint8_t>(counter.load());
  }

  std::uint8_t returnFive() { return 5; }
};

} // namespace

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verifies construction with delegate and label. */
TEST(SchedulableTaskTest, ConstructorSetsLabel) {
  auto delegate = bindFreeFunction(&successTask);
  SchedulableTask task(delegate, "TestLabel");

  EXPECT_EQ(task.getLabel(), "TestLabel");
}

/** @test Verifies empty label is valid. */
TEST(SchedulableTaskTest, ConstructorWithEmptyLabel) {
  auto delegate = bindFreeFunction(&successTask);
  SchedulableTask task(delegate, "");

  EXPECT_TRUE(task.getLabel().empty());
}

/* ----------------------------- Execute Tests ----------------------------- */

/** @test Verifies execute returns task result (success). */
TEST(SchedulableTaskTest, ExecuteReturnsSuccess) {
  auto delegate = bindFreeFunction(&successTask);
  SchedulableTask task(delegate, "success");

  EXPECT_EQ(task.execute(), 0);
}

/** @test Verifies execute returns task result (specific value). */
TEST(SchedulableTaskTest, ExecuteReturnsSpecificValue) {
  auto delegate = bindFreeFunction(&testValueTask);
  SchedulableTask task(delegate, "value42");

  EXPECT_EQ(task.execute(), 42);
}

/** @test Verifies execute returns error code. */
TEST(SchedulableTaskTest, ExecuteReturnsErrorCode) {
  auto delegate = bindFreeFunction(&errorTask);
  SchedulableTask task(delegate, "error");

  EXPECT_EQ(task.execute(), 255);
}

/** @test Verifies multiple execute calls work correctly. */
TEST(SchedulableTaskTest, ExecuteMultipleTimes) {
  TestObject obj;
  auto delegate = bindMember<TestObject, &TestObject::increment>(&obj);
  SchedulableTask task(delegate, "counter");

  EXPECT_EQ(task.execute(), 1);
  EXPECT_EQ(task.execute(), 2);
  EXPECT_EQ(task.execute(), 3);
  EXPECT_EQ(obj.counter.load(), 3);
}

/* ----------------------------- Callable Accessor ----------------------------- */

/** @test Verifies callable() returns working delegate. */
TEST(SchedulableTaskTest, CallableAccessorReturnsDelegate) {
  auto delegate = bindFreeFunction(&testValueTask);
  SchedulableTask task(delegate, "test");

  const auto& callable = task.callable();
  EXPECT_EQ(callable(), 42);
}

/** @test Verifies callable and execute return same result. */
TEST(SchedulableTaskTest, CallableMatchesExecute) {
  auto delegate = bindFreeFunction(&testValueTask);
  SchedulableTask task(delegate, "test");

  EXPECT_EQ(task.callable()(), task.execute());
}

/* ----------------------------- Lambda Binding ----------------------------- */

/** @test Verifies stateless lambda binding works. */
TEST(SchedulableTaskTest, LambdaBindingWorks) {
  auto delegate = bindLambda([]() -> std::uint8_t { return 123; });
  SchedulableTask task(delegate, "lambda");

  EXPECT_EQ(task.execute(), 123);
}

/* ----------------------------- Member Function Binding ----------------------------- */

/** @test Verifies member function binding with object state. */
TEST(SchedulableTaskTest, MemberBindingUpdatesObject) {
  TestObject obj;
  auto delegate = bindMember<TestObject, &TestObject::increment>(&obj);
  SchedulableTask task(delegate, "member");

  EXPECT_EQ(task.execute(), 1);
  EXPECT_EQ(task.execute(), 2);
  EXPECT_EQ(obj.counter.load(), 2);
}

/** @test Verifies multiple objects are independent. */
TEST(SchedulableTaskTest, MemberBindingMultipleObjects) {
  TestObject obj1, obj2;
  auto del1 = bindMember<TestObject, &TestObject::increment>(&obj1);
  auto del2 = bindMember<TestObject, &TestObject::increment>(&obj2);

  SchedulableTask task1(del1, "task1");
  SchedulableTask task2(del2, "task2");

  EXPECT_EQ(task1.execute(), 1);
  EXPECT_EQ(task1.execute(), 2);
  EXPECT_EQ(task2.execute(), 1);

  EXPECT_EQ(obj1.counter.load(), 2);
  EXPECT_EQ(obj2.counter.load(), 1);
}
