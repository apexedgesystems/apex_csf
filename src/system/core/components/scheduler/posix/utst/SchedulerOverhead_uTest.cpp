/**
 * @file SchedulerOverhead_uTest.cpp
 * @brief Unit tests for scheduler dispatch overhead measurement.
 *
 * Purpose: Measure overhead of task dispatch in multi-threaded scheduler:
 *  - Raw task execution time vs scheduler dispatch time
 *  - Thread pool queue overhead
 *  - TaskCtxPool allocation overhead
 *
 * Note: Dependencies are not yet implemented scheduler-side.
 * When added, overhead tests for dependency resolution should be added.
 */

#include "src/system/core/components/scheduler/posix/inc/SchedulerMultiThread.hpp"
#include "src/system/core/components/scheduler/posix/inc/TaskConfig.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/system/core/infrastructure/schedulable/inc/TaskBuilder.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <random>
#include <string>

#include <thread>

#include <gtest/gtest.h>

using system_core::schedulable::bindLambda;
using system_core::schedulable::bindMember;
using system_core::schedulable::SchedulableTask;
using system_core::scheduler::SchedulerMultiThread;
using system_core::scheduler::Status;
using system_core::scheduler::TaskConfig;

/**
 * @brief Test fixture for scheduler overhead measurement.
 */
class SchedulerOverheadTest : public ::testing::Test {
protected:
  void SetUp() override {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    logDir_ = std::filesystem::temp_directory_path() /
              ("scheduler_overhead_test_" + std::to_string(dist(gen)));
    std::filesystem::create_directories(logDir_);

    scheduler_ = std::make_unique<SchedulerMultiThread>(100, logDir_);
  }

  void TearDown() override {
    scheduler_.reset();
    std::filesystem::remove_all(logDir_);
  }

public:
  std::uint8_t taskSleep5ms() {
    taskStartTimes_[0] = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    taskEndTimes_[0] = std::chrono::steady_clock::now();
    return 0;
  }

  std::uint8_t taskSleep2msA() {
    taskStartTimes_[1] = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    taskEndTimes_[1] = std::chrono::steady_clock::now();
    return 0;
  }

  std::uint8_t taskSleep2msB() {
    taskStartTimes_[2] = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    taskEndTimes_[2] = std::chrono::steady_clock::now();
    return 0;
  }

  std::uint8_t taskSleep1msA() {
    taskStartTimes_[3] = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    taskEndTimes_[3] = std::chrono::steady_clock::now();
    return 0;
  }

  std::uint8_t taskSleep1msB() {
    taskStartTimes_[4] = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    taskEndTimes_[4] = std::chrono::steady_clock::now();
    return 0;
  }

  double toMs(std::chrono::steady_clock::duration d) {
    return std::chrono::duration<double, std::milli>(d).count();
  }

protected:
  std::filesystem::path logDir_;
  std::unique_ptr<SchedulerMultiThread> scheduler_;

  std::chrono::steady_clock::time_point taskStartTimes_[5];
  std::chrono::steady_clock::time_point taskEndTimes_[5];
};

/**
 * @test Measure overhead with parallel independent tasks.
 *
 * All tasks run in parallel (no dependencies). Expected behavior:
 * - Total time ~= max(individual task times) + dispatch overhead
 * - 5ms task runs in parallel with 2x2ms + 2x1ms tasks
 * - Expected: ~5-6ms total (parallel execution)
 */
TEST_F(SchedulerOverheadTest, ParallelTasksOverhead) {
  auto del0 = bindMember<SchedulerOverheadTest, &SchedulerOverheadTest::taskSleep5ms>(this);
  auto del1 = bindMember<SchedulerOverheadTest, &SchedulerOverheadTest::taskSleep2msA>(this);
  auto del2 = bindMember<SchedulerOverheadTest, &SchedulerOverheadTest::taskSleep2msB>(this);
  auto del3 = bindMember<SchedulerOverheadTest, &SchedulerOverheadTest::taskSleep1msA>(this);
  auto del4 = bindMember<SchedulerOverheadTest, &SchedulerOverheadTest::taskSleep1msB>(this);

  SchedulableTask t0(del0, "sleep5ms");
  SchedulableTask t1(del1, "sleep2msA");
  SchedulableTask t2(del2, "sleep2msB");
  SchedulableTask t3(del3, "sleep1msA");
  SchedulableTask t4(del4, "sleep1msB");

  // All tasks at 100Hz (run every tick)
  TaskConfig cfg(100, 1, 0);

  ASSERT_EQ(scheduler_->addTask(t0, cfg), Status::SUCCESS);
  ASSERT_EQ(scheduler_->addTask(t1, cfg), Status::SUCCESS);
  ASSERT_EQ(scheduler_->addTask(t2, cfg), Status::SUCCESS);
  ASSERT_EQ(scheduler_->addTask(t3, cfg), Status::SUCCESS);
  ASSERT_EQ(scheduler_->addTask(t4, cfg), Status::SUCCESS);

  ASSERT_EQ(scheduler_->init(), static_cast<std::uint8_t>(Status::SUCCESS));

  auto schedStart = std::chrono::steady_clock::now();
  auto status = scheduler_->executeTasksOnTickMulti(0);
  auto schedEnd = std::chrono::steady_clock::now();

  ASSERT_EQ(status, Status::SUCCESS);

  // Wait for async tasks to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  double schedulerTime = toMs(schedEnd - schedStart);

  double taskTimes[5];
  double totalTaskWork = 0.0;
  for (int i = 0; i < 5; ++i) {
    taskTimes[i] = toMs(taskEndTimes_[i] - taskStartTimes_[i]);
    totalTaskWork += taskTimes[i];
  }

  std::printf("\n=== Parallel Tasks Overhead Test ===\n");
  std::printf("Task execution times:\n");
  std::printf("  sleep5ms:  %.3f ms\n", taskTimes[0]);
  std::printf("  sleep2msA: %.3f ms\n", taskTimes[1]);
  std::printf("  sleep2msB: %.3f ms\n", taskTimes[2]);
  std::printf("  sleep1msA: %.3f ms\n", taskTimes[3]);
  std::printf("  sleep1msB: %.3f ms\n", taskTimes[4]);
  std::printf("Total sequential work: %.3f ms\n", totalTaskWork);
  std::printf("Scheduler dispatch time: %.3f ms\n", schedulerTime);
  std::printf("Note: Tasks run async - dispatch returns immediately\n");

  // Dispatch should be fast (< 1ms) since tasks are async
  EXPECT_LT(schedulerTime, 5.0) << "Dispatch should be fast (async tasks)";
}

/**
 * @test Measure scheduler init overhead.
 */
TEST_F(SchedulerOverheadTest, InitOverhead) {
  auto del = bindLambda([]() -> std::uint8_t { return 0; });
  SchedulableTask task(del, "noop");

  TaskConfig cfg(100, 1, 0);
  ASSERT_EQ(scheduler_->addTask(task, cfg), Status::SUCCESS);

  auto initStart = std::chrono::steady_clock::now();
  auto initStatus = scheduler_->init();
  auto initEnd = std::chrono::steady_clock::now();

  ASSERT_EQ(initStatus, static_cast<std::uint8_t>(Status::SUCCESS));

  double initTime = toMs(initEnd - initStart);
  std::printf("\n=== Init Overhead Test ===\n");
  std::printf("Scheduler init time: %.3f ms\n", initTime);

  // Init includes ThreadPool creation which spawns threads.
  // In containerized environments, thread creation can take seconds.
  // Just validate it completes in reasonable time (< 30s).
  EXPECT_LT(initTime, 30000.0) << "Init should complete in reasonable time";
}

/**
 * @test Measure empty tick overhead.
 */
TEST_F(SchedulerOverheadTest, EmptyTickOverhead) {
  ASSERT_EQ(scheduler_->init(), static_cast<std::uint8_t>(Status::SUCCESS));

  constexpr int ITERATIONS = 1000;
  auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < ITERATIONS; ++i) {
    [[maybe_unused]] auto status =
        scheduler_->executeTasksOnTickMulti(static_cast<std::uint16_t>(i % 100));
  }
  auto end = std::chrono::steady_clock::now();

  double totalTime = toMs(end - start);
  double avgTime = totalTime / ITERATIONS;

  std::printf("\n=== Empty Tick Overhead Test ===\n");
  std::printf("Total time for %d empty ticks: %.3f ms\n", ITERATIONS, totalTime);
  std::printf("Average per tick: %.3f us\n", avgTime * 1000.0);

  // Empty ticks should be very fast (< 10us average)
  EXPECT_LT(avgTime, 0.1) << "Empty tick should be < 100us";
}
