/**
 * @file SchedulerRequirements_uTest.cpp
 * @brief Unit tests for the published requirements + preflight opcodes.
 *
 * Coverage:
 *  - GET_REQUIREMENTS reflects the constructed pools' requested
 *    policy/priority/affinity and worker counts plus table facts
 *    (task count, sequence groups, fundamental frequency).
 *  - RUN_PREFLIGHT / GET_PREFLIGHT round-trip the table verdicts on
 *    the generic command path.
 */

#include "src/system/core/components/scheduler/posix/inc/SchedulerMultiThread.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerPreflight.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerTlm.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SequenceGroup.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using apex::concurrency::DelegateU8;
using apex::concurrency::PoolConfig;
using system_core::schedulable::SchedulableTask;
using system_core::schedulable::SequenceGroup;
using system_core::scheduler::PreflightVerdict;
using system_core::scheduler::SchedulerMultiThread;
using system_core::scheduler::SchedulerRequirementsTlm;
using system_core::scheduler::SchedulerTlmOpcode;
using system_core::scheduler::Status;
using system_core::scheduler::TablePreflight;
using system_core::scheduler::TaskConfig;

namespace {

std::uint8_t noopTask(void*) noexcept { return 0; }

std::filesystem::path testLogDir() {
  const auto DIR = std::filesystem::temp_directory_path() /
                   ("scheduler_requirements_utest_" + std::to_string(::getpid()));
  std::filesystem::create_directories(DIR);
  return DIR;
}

} // namespace

/** @test Requirements rows reflect specs, table facts, and workers. */
TEST(SchedulerRequirementsTest, RowsReflectSpecsAndTable) {
  PoolConfig rt{};
  rt.policy = 1; // FIFO request.
  rt.priority = 80;
  rt.affinity = {0U, 1U};

  std::vector<system_core::scheduler::PoolSpec> specs;
  specs.push_back({"rt", 2, rt});
  specs.push_back({"bulk", 1, {}});

  SequenceGroup seq(2);
  SchedulerMultiThread sched(100, testLogDir(), specs);
  ASSERT_EQ(sched.init(), 0);

  DelegateU8 del{&noopTask, nullptr};
  SchedulableTask a(del, "a");
  SchedulableTask b(del, "b");
  seq.addTask(a, 1);
  seq.addTask(b, 2);
  ASSERT_EQ(sched.addTask(a, TaskConfig(100, 1, 0), &seq), Status::SUCCESS);
  ASSERT_EQ(sched.addTask(b, TaskConfig(100, 1, 0), &seq), Status::SUCCESS);

  std::vector<std::uint8_t> resp;
  ASSERT_EQ(sched.handleCommand(static_cast<std::uint16_t>(SchedulerTlmOpcode::GET_REQUIREMENTS),
                                {}, resp),
            0);
  ASSERT_EQ(resp.size(), sizeof(SchedulerRequirementsTlm));
  SchedulerRequirementsTlm tlm{};
  std::memcpy(&tlm, resp.data(), sizeof(tlm));

  EXPECT_EQ(tlm.fundamentalFreqHz, 100U);
  EXPECT_EQ(tlm.taskCount, 2U);
  EXPECT_EQ(tlm.seqGroupCount, 1U);
  ASSERT_EQ(tlm.poolCount, 2U);
  EXPECT_EQ(tlm.pools[0].policy, 1U);
  EXPECT_EQ(tlm.pools[0].priority, 80);
  EXPECT_EQ(tlm.pools[0].workers, 2U);
  EXPECT_EQ(tlm.pools[0].affinityMask, 0b11ULL);
  EXPECT_EQ(tlm.pools[1].policy, 0U);
  EXPECT_EQ(tlm.pools[1].workers, 1U);
  EXPECT_EQ(tlm.pools[1].affinityMask, 0ULL);

  sched.shutdown();
}

/** @test RUN_PREFLIGHT and GET_PREFLIGHT round-trip the verdicts. */
TEST(SchedulerRequirementsTest, PreflightOpcodesRoundTrip) {
  SchedulerMultiThread sched(100, testLogDir());
  ASSERT_EQ(sched.init(), 0);

  DelegateU8 del{&noopTask, nullptr};
  SchedulableTask task(del, "solo");
  ASSERT_EQ(sched.addTask(task, TaskConfig(100, 1, 0)), Status::SUCCESS);

  std::vector<std::uint8_t> resp;
  ASSERT_EQ(
      sched.handleCommand(static_cast<std::uint16_t>(SchedulerTlmOpcode::RUN_PREFLIGHT), {}, resp),
      0);
  ASSERT_EQ(resp.size(), sizeof(TablePreflight));
  TablePreflight run{};
  std::memcpy(&run, resp.data(), sizeof(run));
  EXPECT_EQ(run.overall(), PreflightVerdict::PASS);

  resp.clear();
  ASSERT_EQ(
      sched.handleCommand(static_cast<std::uint16_t>(SchedulerTlmOpcode::GET_PREFLIGHT), {}, resp),
      0);
  ASSERT_EQ(resp.size(), sizeof(TablePreflight));
  TablePreflight got{};
  std::memcpy(&got, resp.data(), sizeof(got));
  EXPECT_EQ(got.overall(), PreflightVerdict::PASS);

  sched.shutdown();
}
