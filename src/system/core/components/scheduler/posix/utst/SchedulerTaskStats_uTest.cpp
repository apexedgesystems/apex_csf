/**
 * @file SchedulerTaskStats_uTest.cpp
 * @brief Unit tests for per-task runtime stats and the loss diagnostics.
 *
 * Coverage:
 *  - Completion path populates lastRuntime/maxRuntime/completions.
 *  - A task still running at re-dispatch counts a per-task deadline
 *    violation (the attribution the field FATALs rely on).
 *  - GET_TASK_STATS returns a populated snapshot row for the task.
 *  - inFlightSummary names an in-flight task and reads "none" idle.
 */

#include "src/system/core/components/scheduler/posix/inc/SchedulerMultiThread.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerTlm.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>
#include <vector>

using apex::concurrency::DelegateU8;
using system_core::schedulable::SchedulableTask;
using system_core::scheduler::SchedulerMultiThread;
using system_core::scheduler::SchedulerTaskStatsTlm;
using system_core::scheduler::SchedulerTlmOpcode;
using system_core::scheduler::Status;
using system_core::scheduler::TaskConfig;

namespace {

std::atomic<bool> gHold{false};

std::uint8_t quickTask(void*) noexcept { return 0; }

std::uint8_t holdTask(void*) noexcept {
  while (gHold.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return 0;
}

std::filesystem::path testLogDir() {
  const auto DIR = std::filesystem::temp_directory_path() / "scheduler_taskstats_utest";
  std::filesystem::create_directories(DIR);
  return DIR;
}

void waitForCompletions(SchedulerMultiThread& sched, std::uint32_t want) {
  for (int i = 0; i < 2000; ++i) {
    sched.populateTaskStatsTlm();
    std::vector<std::uint8_t> resp;
    if (sched.handleCommand(static_cast<std::uint16_t>(SchedulerTlmOpcode::GET_TASK_STATS), {},
                            resp) == 0 &&
        resp.size() == sizeof(SchedulerTaskStatsTlm)) {
      SchedulerTaskStatsTlm tlm{};
      std::memcpy(&tlm, resp.data(), sizeof(tlm));
      if (tlm.taskCount > 0 && tlm.rows[0].maxRuntimeUs > 0) {
        return;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    (void)want;
  }
}

} // namespace

/** @test Completion populates runtime counters and the stats snapshot. */
TEST(SchedulerTaskStatsTest, CompletionPopulatesCounters) {
  SchedulerMultiThread sched(100, testLogDir());
  ASSERT_EQ(sched.init(), 0);

  DelegateU8 del{&quickTask, nullptr};
  SchedulableTask task(del, "quick");
  ASSERT_EQ(sched.addTask(task, TaskConfig(100, 1, 0)), Status::SUCCESS);

  (void)sched.executeTasksOnTickMulti(0);
  waitForCompletions(sched, 1);

  std::vector<std::uint8_t> resp;
  ASSERT_EQ(
      sched.handleCommand(static_cast<std::uint16_t>(SchedulerTlmOpcode::GET_TASK_STATS), {}, resp),
      0);
  ASSERT_EQ(resp.size(), sizeof(SchedulerTaskStatsTlm));
  SchedulerTaskStatsTlm tlm{};
  std::memcpy(&tlm, resp.data(), sizeof(tlm));

  EXPECT_EQ(tlm.taskCount, 1U);
  EXPECT_EQ(tlm.truncated, 0U);
  EXPECT_GT(tlm.rows[0].maxRuntimeUs, 0U);
  EXPECT_EQ(tlm.rows[0].violations16, 0U);

  sched.shutdown();
}

/** @test A task still running at re-dispatch counts its own violation. */
TEST(SchedulerTaskStatsTest, StillRunningCountsPerTaskViolation) {
  SchedulerMultiThread sched(100, testLogDir());
  ASSERT_EQ(sched.init(), 0);

  gHold.store(true, std::memory_order_release);
  DelegateU8 del{&holdTask, nullptr};
  SchedulableTask task(del, "holder");
  ASSERT_EQ(sched.addTask(task, TaskConfig(100, 1, 0)), Status::SUCCESS);

  (void)sched.executeTasksOnTickMulti(0);
  std::this_thread::sleep_for(std::chrono::milliseconds(20)); // Let it start and hold.
  (void)sched.executeTasksOnTickMulti(0); // Re-dispatch: still running -> violation.

  std::vector<std::uint8_t> resp;
  ASSERT_EQ(
      sched.handleCommand(static_cast<std::uint16_t>(SchedulerTlmOpcode::GET_TASK_STATS), {}, resp),
      0);
  SchedulerTaskStatsTlm tlm{};
  std::memcpy(&tlm, resp.data(), sizeof(tlm));
  EXPECT_GE(tlm.rows[0].violations16, 1U);

  // The violated task is also what in-flight diagnostics must name.
  EXPECT_NE(sched.inFlightSummary(4).find(":"), std::string::npos);

  gHold.store(false, std::memory_order_release);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  sched.shutdown();
}

/** @test Idle scheduler reports no in-flight tasks. */
TEST(SchedulerTaskStatsTest, InFlightSummaryIdleReadsNone) {
  SchedulerMultiThread sched(100, testLogDir());
  ASSERT_EQ(sched.init(), 0);
  EXPECT_EQ(sched.inFlightSummary(4), "none");
  sched.shutdown();
}
