/**
 * @file SchedulerCensus_uTest.cpp
 * @brief Unit tests for the pre-clock task census.
 *
 * Coverage:
 *  - Census measures first vs steady cost; a task with expensive
 *    first-call init reports WARN (startup violation expected) while
 *    steady state passes -- the cold-start class the census exists
 *    to catch before the clock does.
 *  - A task whose steady cost exceeds its period budget reports FAIL.
 *  - Census refuses to run once ticks have executed (strictly
 *    pre-clock).
 *  - RUN/GET opcodes round-trip the report on the generic command path.
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
using system_core::scheduler::SchedulerCensusTlm;
using system_core::scheduler::SchedulerMultiThread;
using system_core::scheduler::SchedulerTlmOpcode;
using system_core::scheduler::Status;
using system_core::scheduler::TaskConfig;

namespace {

std::uint8_t quickTask(void*) noexcept { return 0; }

// First call sleeps (lazy-init stand-in); later calls are cheap.
std::uint8_t coldStartTask(void* ctx) noexcept {
  auto* first = static_cast<std::atomic<bool>*>(ctx);
  if (first->exchange(false, std::memory_order_acq_rel)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
  return 0;
}

std::uint8_t alwaysSlowTask(void*) noexcept {
  std::this_thread::sleep_for(std::chrono::milliseconds(15));
  return 0;
}

std::filesystem::path testLogDir() {
  const auto DIR = std::filesystem::temp_directory_path() / "scheduler_census_utest";
  std::filesystem::create_directories(DIR);
  return DIR;
}

SchedulerCensusTlm getCensus(SchedulerMultiThread& sched) {
  std::vector<std::uint8_t> resp;
  EXPECT_EQ(sched.handleCommand(static_cast<std::uint16_t>(SchedulerTlmOpcode::GET_TASK_CENSUS), {},
                                resp),
            0);
  SchedulerCensusTlm tlm{};
  EXPECT_EQ(resp.size(), sizeof(tlm));
  std::memcpy(&tlm, resp.data(), sizeof(tlm));
  return tlm;
}

} // namespace

/** @test Cold first call WARNs; steady state passes the budget. */
TEST(SchedulerCensusTest, ColdStartWarnsSteadyPasses) {
  SchedulerMultiThread sched(100, testLogDir());
  ASSERT_EQ(sched.init(), 0);

  std::atomic<bool> first{true};
  DelegateU8 del{&coldStartTask, &first};
  SchedulableTask task(del, "coldstart");
  // 10 Hz -> 100 ms period budget; 30 ms cold start fits, so use a
  // tighter task: 100 Hz -> 10 ms budget; 30 ms cold start exceeds it.
  ASSERT_EQ(sched.addTask(task, TaskConfig(100, 1, 0)), Status::SUCCESS);

  ASSERT_TRUE(sched.runTaskCensus(3));
  const auto TLM = getCensus(sched);

  ASSERT_EQ(TLM.taskCount, 1U);
  EXPECT_EQ(TLM.rows[0].verdict, 1U); // WARN: first over budget, steady under.
  EXPECT_GT(TLM.rows[0].firstUs, TLM.rows[0].budgetUs);
  EXPECT_LT(TLM.rows[0].steadyUs, TLM.rows[0].budgetUs);
  EXPECT_EQ(TLM.overall, 1U);

  sched.shutdown();
}

/** @test Steady cost over the budget FAILs the row and the report. */
TEST(SchedulerCensusTest, SteadyOverBudgetFails) {
  SchedulerMultiThread sched(100, testLogDir());
  ASSERT_EQ(sched.init(), 0);

  DelegateU8 del{&alwaysSlowTask, nullptr};
  SchedulableTask task(del, "alwaysslow");
  ASSERT_EQ(sched.addTask(task, TaskConfig(100, 1, 0)), Status::SUCCESS); // 10 ms budget.

  ASSERT_TRUE(sched.runTaskCensus(2));
  const auto TLM = getCensus(sched);

  ASSERT_EQ(TLM.taskCount, 1U);
  EXPECT_EQ(TLM.rows[0].verdict, 2U);
  EXPECT_GT(TLM.rows[0].steadyUs, TLM.rows[0].budgetUs);
  EXPECT_EQ(TLM.overall, 2U);

  sched.shutdown();
}

/** @test Census is strictly pre-clock: refused after ticks execute. */
TEST(SchedulerCensusTest, RefusedAfterTicksExecute) {
  SchedulerMultiThread sched(100, testLogDir());
  ASSERT_EQ(sched.init(), 0);

  DelegateU8 del{&quickTask, nullptr};
  SchedulableTask task(del, "quick");
  ASSERT_EQ(sched.addTask(task, TaskConfig(100, 1, 0)), Status::SUCCESS);

  (void)sched.executeTasksOnTickMulti(0);
  EXPECT_FALSE(sched.runTaskCensus(1));

  // The RUN opcode reports the refusal on the command path too.
  std::vector<std::uint8_t> resp;
  EXPECT_NE(sched.handleCommand(static_cast<std::uint16_t>(SchedulerTlmOpcode::RUN_TASK_CENSUS), {},
                                resp),
            0);

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  sched.shutdown();
}
