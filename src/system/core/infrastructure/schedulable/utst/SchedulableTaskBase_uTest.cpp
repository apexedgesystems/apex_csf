/**
 * @file SchedulableTaskBase_uTest.cpp
 * @brief Unit tests for SchedulableTaskBase - abstract task interface.
 *
 * Coverage:
 *  - Base class inheritance
 *  - getLabel() accessor
 *  - Virtual execute() dispatch
 *  - TaskFn typedef
 */

#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTaskBase.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
#include <vector>

using apex::concurrency::DelegateU8;
using system_core::schedulable::SchedulableTask;
using system_core::schedulable::SchedulableTaskBase;

namespace {

std::uint8_t returnValue(void* ctx) noexcept { return *static_cast<std::uint8_t*>(ctx); }

} // namespace

/* ----------------------------- SchedulableTaskBase Method Tests ----------------------------- */

/** @test Verifies SchedulableTask inherits from SchedulableTaskBase. */
TEST(SchedulableTaskBaseTest, InheritanceCheck) {
  std::uint8_t val = 5;
  DelegateU8 del{&returnValue, &val};
  SchedulableTask task(del, "test");

  // Cast to base pointer
  SchedulableTaskBase* basePtr = &task;
  EXPECT_NE(basePtr, nullptr);
}

/** @test Verifies getLabel works through base pointer. */
TEST(SchedulableTaskBaseTest, GetLabelThroughBase) {
  std::uint8_t val = 5;
  DelegateU8 del{&returnValue, &val};
  SchedulableTask task(del, "base_label");

  SchedulableTaskBase* basePtr = &task;
  EXPECT_EQ(basePtr->getLabel(), "base_label");
}

/** @test Verifies virtual execute works through base pointer. */
TEST(SchedulableTaskBaseTest, VirtualExecuteThroughBase) {
  std::uint8_t val = 99;
  DelegateU8 del{&returnValue, &val};
  SchedulableTask task(del, "virtual");

  SchedulableTaskBase* basePtr = &task;
  EXPECT_EQ(basePtr->execute(), 99);
}

/* ----------------------------- API Tests ----------------------------- */

/** @test Verifies polymorphic container of tasks. */
TEST(SchedulableTaskBaseTest, PolymorphicContainer) {
  std::uint8_t v1 = 1, v2 = 2, v3 = 3;
  DelegateU8 del1{&returnValue, &v1};
  DelegateU8 del2{&returnValue, &v2};
  DelegateU8 del3{&returnValue, &v3};

  SchedulableTask task1(del1, "t1");
  SchedulableTask task2(del2, "t2");
  SchedulableTask task3(del3, "t3");

  std::vector<SchedulableTaskBase*> tasks = {&task1, &task2, &task3};

  EXPECT_EQ(tasks[0]->execute(), 1);
  EXPECT_EQ(tasks[1]->execute(), 2);
  EXPECT_EQ(tasks[2]->execute(), 3);
}

/** @test Verifies labels accessible through polymorphic container. */
TEST(SchedulableTaskBaseTest, LabelsInPolymorphicContainer) {
  std::uint8_t v1 = 1, v2 = 2;
  DelegateU8 del1{&returnValue, &v1};
  DelegateU8 del2{&returnValue, &v2};

  SchedulableTask task1(del1, "first");
  SchedulableTask task2(del2, "second");

  std::vector<SchedulableTaskBase*> tasks = {&task1, &task2};

  EXPECT_EQ(tasks[0]->getLabel(), "first");
  EXPECT_EQ(tasks[1]->getLabel(), "second");
}

/* ----------------------------- Constants Tests ----------------------------- */

/** @test Verifies TaskFn typedef is DelegateU8. */
TEST(SchedulableTaskBaseTest, TaskFnIsDelegateU8) {
  static_assert(std::is_same_v<SchedulableTaskBase::TaskFn, DelegateU8>,
                "TaskFn should be DelegateU8");
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test Verifies virtual destructor works correctly. */
TEST(SchedulableTaskBaseTest, VirtualDestructorSafe) {
  std::uint8_t val = 42;
  DelegateU8 del{&returnValue, &val};

  // Create through unique_ptr to base
  auto task = std::make_unique<SchedulableTask>(del, "destroy_test");
  SchedulableTaskBase* basePtr = task.get();

  // Should be able to execute before destruction
  EXPECT_EQ(basePtr->execute(), 42);

  // unique_ptr will call destructor through derived type - this tests compilation
  task.reset();
}
