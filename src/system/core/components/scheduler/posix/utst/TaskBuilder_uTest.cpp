/**
 * @file TaskBuilder_uTest.cpp
 * @brief Unit tests for TaskBuilder helpers (bindMember, bindLambda, bindFreeFunction).
 *
 * Coverage:
 *  - bindMember with member functions
 *  - bindLambda with stateless lambdas
 *  - bindFreeFunction with C-style functions
 *  - Integration with SchedulableTask
 *  - Multiple objects and delegate types
 */

#include "src/system/core/infrastructure/schedulable/inc/TaskBuilder.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"

#include <cstdint>

#include <atomic>

#include <gtest/gtest.h>

using system_core::schedulable::bindFreeFunction;
using system_core::schedulable::bindLambda;
using system_core::schedulable::bindMember;
using system_core::schedulable::SchedulableTask;

namespace {

struct TestObject {
  std::uint8_t counter{0};

  std::uint8_t increment() {
    ++counter;
    return counter;
  }

  std::uint8_t returnFive() { return 5; }
};

std::uint8_t freeFunction() { return 42; }

} // namespace

/* ----------------------------- bindMember Tests ----------------------------- */

/** @test Verifies bindMember calls member function and updates object state. */
TEST(TaskBuilderTest, BindMemberCallsFunction) {
  TestObject obj;
  auto delegate = bindMember<TestObject, &TestObject::increment>(&obj);

  EXPECT_EQ(delegate(), 1);
  EXPECT_EQ(delegate(), 2);
  EXPECT_EQ(obj.counter, 2);
}

/** @test Verifies bindMember integrates with SchedulableTask. */
TEST(TaskBuilderTest, BindMemberIntegratesWithSchedulableTask) {
  TestObject obj;
  auto delegate = bindMember<TestObject, &TestObject::returnFive>(&obj);
  SchedulableTask task(delegate, "test_task");

  EXPECT_EQ(task.execute(), 5);
}

/** @test Verifies bindMember handles multiple objects independently. */
TEST(TaskBuilderTest, BindMemberMultipleObjects) {
  TestObject obj1, obj2;
  auto del1 = bindMember<TestObject, &TestObject::increment>(&obj1);
  auto del2 = bindMember<TestObject, &TestObject::increment>(&obj2);

  EXPECT_EQ(del1(), 1);
  EXPECT_EQ(del2(), 1);
  EXPECT_EQ(del1(), 2);
  EXPECT_EQ(obj1.counter, 2);
  EXPECT_EQ(obj2.counter, 1);
}

/* ----------------------------- bindLambda Tests ----------------------------- */

/** @test Verifies bindLambda wraps stateless lambda correctly. */
TEST(TaskBuilderTest, BindLambdaStatelessLambda) {
  auto delegate = bindLambda([]() -> std::uint8_t { return 100; });

  EXPECT_EQ(delegate(), 100);
}

/** @test Verifies bindLambda integrates with SchedulableTask. */
TEST(TaskBuilderTest, BindLambdaIntegratesWithSchedulableTask) {
  auto delegate = bindLambda([]() -> std::uint8_t { return 123; });
  SchedulableTask task(delegate, "lambda_task");

  EXPECT_EQ(task.execute(), 123);
}

/* ----------------------------- bindFreeFunction Tests ----------------------------- */

/** @test Verifies bindFreeFunction wraps C-style function correctly. */
TEST(TaskBuilderTest, BindFreeFunctionCallsFunction) {
  auto delegate = bindFreeFunction(&freeFunction);

  EXPECT_EQ(delegate(), 42);
}

/** @test Verifies bindFreeFunction integrates with SchedulableTask. */
TEST(TaskBuilderTest, BindFreeFunctionIntegratesWithSchedulableTask) {
  auto delegate = bindFreeFunction(&freeFunction);
  SchedulableTask task(delegate, "free_function_task");

  EXPECT_EQ(task.execute(), 42);
}

/* ----------------------------- Mixed Delegate Workflow ----------------------------- */

/** @test Verifies mixing different delegate types in same workflow. */
TEST(TaskBuilderTest, FullWorkflowMixedDelegateTypes) {
  TestObject obj;
  auto memDelegate = bindMember<TestObject, &TestObject::returnFive>(&obj);
  auto lambdaDelegate = bindLambda([]() -> std::uint8_t { return 10; });
  auto freeDelegate = bindFreeFunction(&freeFunction);

  SchedulableTask task1(memDelegate, "member");
  SchedulableTask task2(lambdaDelegate, "lambda");
  SchedulableTask task3(freeDelegate, "free");

  EXPECT_EQ(task1.execute(), 5);
  EXPECT_EQ(task2.execute(), 10);
  EXPECT_EQ(task3.execute(), 42);
}

/** @test Verifies full workflow with member function binding. */
TEST(TaskBuilderTest, FullWorkflowMemberFunction) {
  struct Workflow {
    std::uint8_t stepCounter{0};
    std::uint8_t step1() { return ++stepCounter; }
    std::uint8_t step2() { return ++stepCounter; }
  };

  Workflow wf;
  auto del1 = bindMember<Workflow, &Workflow::step1>(&wf);
  auto del2 = bindMember<Workflow, &Workflow::step2>(&wf);

  SchedulableTask task1(del1, "step1");
  SchedulableTask task2(del2, "step2");

  EXPECT_EQ(task1.getLabel(), "step1");
  EXPECT_EQ(task2.getLabel(), "step2");
  EXPECT_EQ(task1.execute(), 1);
  EXPECT_EQ(task2.execute(), 2);
}
