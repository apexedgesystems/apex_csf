/**
 * @file SchedulerCtxPool_uTest.cpp
 * @brief Unit tests for TaskCtx pool capacity and dispatch-drop behavior.
 *
 * Coverage:
 *   - TaskCtxPool exhaustion contract (no fallback: acquire returns null,
 *     release makes the context reacquirable)
 *   - Context capacity covers the schedule's densest tick: a single-tick
 *     burst wider than the worker count dispatches every task with zero
 *     drops and zero fallback allocations
 *   - Nothing is left mid-flight after the burst: the next dispatch tick
 *     sees no period violations and no skips
 */

#include "src/system/core/components/scheduler/posix/inc/SchedulerMultiThread.hpp"
#include "src/system/core/components/scheduler/posix/inc/TaskConfig.hpp"
#include "src/system/core/components/scheduler/posix/inc/TaskCtxPool.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/system/core/infrastructure/schedulable/inc/TaskBuilder.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using system_core::schedulable::bindMember;
using system_core::schedulable::SchedulableTask;
using system_core::scheduler::PoolSpec;
using system_core::scheduler::SchedulerMultiThread;
using system_core::scheduler::Status;
using system_core::scheduler::TaskConfig;
using system_core::scheduler::TaskCtxPool;

/* ----------------------------- TaskCtxPool Contract ----------------------------- */

/** @test With fallback disabled, an exhausted pool returns null instead of allocating. */
TEST(TaskCtxPoolTest, ExhaustedPoolReturnsNullWithoutFallback) {
  TaskCtxPool pool;
  pool.preallocate(2);
  pool.setFallbackAlloc(false);

  auto* a = pool.acquire(nullptr, nullptr);
  auto* b = pool.acquire(nullptr, nullptr);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);

  EXPECT_EQ(pool.acquire(nullptr, nullptr), nullptr);

  pool.release(a);
  auto* c = pool.acquire(nullptr, nullptr);
  EXPECT_NE(c, nullptr);

  pool.release(b);
  pool.release(c);
}

/** @test With fallback enabled, an exhausted pool grows instead of failing. */
TEST(TaskCtxPoolTest, ExhaustedPoolGrowsWithFallback) {
  TaskCtxPool pool;
  pool.preallocate(1);
  pool.setFallbackAlloc(true);

  auto* a = pool.acquire(nullptr, nullptr);
  auto* b = pool.acquire(nullptr, nullptr);
  EXPECT_NE(a, nullptr);
  EXPECT_NE(b, nullptr);

  pool.release(a);
  pool.release(b);
}

/* ----------------------------- Dense-Tick Burst ----------------------------- */

/**
 * @brief Fixture driving a single-tick burst far wider than the worker count.
 *
 * One worker, twelve every-tick tasks: the dispatch loop demands twelve
 * contexts before the worker completes a single task, so capacity sized
 * from worker count alone is exhausted mid-tick. Each task holds its
 * context briefly to keep the full burst outstanding at once.
 */
class SchedulerCtxBurstTest : public ::testing::Test {
protected:
  static constexpr std::size_t BURST_TASKS = 12;

  void SetUp() override {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    logDir_ = std::filesystem::temp_directory_path() /
              ("scheduler_ctx_burst_test_" + std::to_string(dist(gen)));
    std::filesystem::create_directories(logDir_);

    std::vector<PoolSpec> pools;
    pools.push_back({"burst", 1, {}});
    scheduler_ = std::make_unique<SchedulerMultiThread>(100, logDir_, std::move(pools));
  }

  void TearDown() override {
    scheduler_.reset();
    std::filesystem::remove_all(logDir_);
  }

public:
  std::uint8_t holdAndCount() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    tasksCompleted_.fetch_add(1, std::memory_order_release);
    return 0;
  }

protected:
  void waitForCompletions(std::size_t expected) {
    const auto DEADLINE = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (tasksCompleted_.load(std::memory_order_acquire) < expected) {
      ASSERT_LT(std::chrono::steady_clock::now(), DEADLINE) << "tasks did not complete in time";
      std::this_thread::yield();
    }
  }

  std::filesystem::path logDir_;
  std::unique_ptr<SchedulerMultiThread> scheduler_;
  std::atomic<std::size_t> tasksCompleted_{0};
};

/**
 * @test Every task of the densest tick dispatches: no drops, no fallback.
 *
 * The fallback-allocation counter pins capacity in debug builds, where
 * heap fallback would otherwise mask an under-sized pool: if the count
 * rises above its post-init value, dispatch demand exceeded preallocated
 * capacity, and the same schedule drops tasks in production builds where
 * fallback is disabled.
 */
TEST_F(SchedulerCtxBurstTest, DenseTickDispatchesAllTasksWithoutFallback) {
  auto del = bindMember<SchedulerCtxBurstTest, &SchedulerCtxBurstTest::holdAndCount>(this);

  std::vector<std::unique_ptr<SchedulableTask>> tasks;
  std::vector<std::string> labels;
  tasks.reserve(BURST_TASKS);
  labels.reserve(BURST_TASKS);
  TaskConfig cfg(100, 1, 0);
  for (std::size_t i = 0; i < BURST_TASKS; ++i) {
    labels.push_back("burst" + std::to_string(i));
    tasks.push_back(std::make_unique<SchedulableTask>(del, labels.back().c_str()));
    ASSERT_EQ(scheduler_->addTask(*tasks.back(), cfg), Status::SUCCESS);
  }

  ASSERT_EQ(scheduler_->init(), static_cast<std::uint8_t>(Status::SUCCESS));
  const std::size_t PREALLOCATED = scheduler_->ctxPoolAllocatedCount(0);

  ASSERT_EQ(scheduler_->executeTasksOnTickMulti(0), Status::SUCCESS);
  waitForCompletions(BURST_TASKS);

  EXPECT_EQ(scheduler_->totalDispatchDrops(), 0U);
  EXPECT_EQ(scheduler_->ctxPoolAllocatedCount(0), PREALLOCATED)
      << "fallback allocation fired: context capacity does not cover the densest tick";

  // The next period must see a clean slate: no entry left marked
  // in-flight (the poison signature of a dropped dispatch), no skips.
  ASSERT_EQ(scheduler_->executeTasksOnTickMulti(1), Status::SUCCESS);
  waitForCompletions(2 * BURST_TASKS);
  EXPECT_EQ(scheduler_->periodViolationsThisTick(), 0U);
  EXPECT_EQ(scheduler_->totalSkipCount(), 0U);
  EXPECT_EQ(scheduler_->totalDispatchDrops(), 0U);

  scheduler_->shutdown();
}
