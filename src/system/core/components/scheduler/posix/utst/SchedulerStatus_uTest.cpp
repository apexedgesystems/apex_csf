/**
 * @file SchedulerStatus_uTest.cpp
 * @brief Unit tests for SchedulerStatus enum and toString function.
 *
 * Coverage:
 *   - All Status enum values
 *   - toString() returns correct strings
 *   - Enum value continuity and base offset
 */

#include "src/system/core/components/scheduler/posix/inc/SchedulerStatus.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"

#include <cstdint>

#include <gtest/gtest.h>

using system_core::scheduler::Status;
using system_core::scheduler::toString;

/* ----------------------------- Enum Value Tests ----------------------------- */

/** @test SUCCESS is zero. */
TEST(SchedulerStatusTest, SuccessIsZero) {
  EXPECT_EQ(static_cast<std::uint8_t>(Status::SUCCESS), 0);
}

/** @test First error starts after SystemComponentStatus::EOE_SYSTEM_COMPONENT. */
TEST(SchedulerStatusTest, ErrorsStartAfterSystemComponentEOE) {
  const auto EOE =
      static_cast<std::uint8_t>(system_core::system_component::Status::EOE_SYSTEM_COMPONENT);
  EXPECT_EQ(static_cast<std::uint8_t>(Status::ERROR_TFREQN_GT_FFREQ), EOE);
}

/** @test EOE_SCHEDULER is the highest value. */
TEST(SchedulerStatusTest, EOEIsHighestValue) {
  EXPECT_GT(static_cast<std::uint8_t>(Status::EOE_SCHEDULER),
            static_cast<std::uint8_t>(Status::WARN_PERIOD_VIOLATION));
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test toString returns correct string for SUCCESS. */
TEST(SchedulerStatusTest, ToStringSuccess) { EXPECT_STREQ(toString(Status::SUCCESS), "SUCCESS"); }

/** @test toString returns correct string for scheduler base errors. */
TEST(SchedulerStatusTest, ToStringSchedulerBaseErrors) {
  EXPECT_STREQ(toString(Status::ERROR_TFREQN_GT_FFREQ), "ERROR_TFREQN_GT_FFREQ");
  EXPECT_STREQ(toString(Status::ERROR_FFREQ_MOD_TFREQN_DNE0), "ERROR_FFREQ_MOD_TFREQN_DNE0");
  EXPECT_STREQ(toString(Status::ERROR_OFFSET_GTE_TPT), "ERROR_OFFSET_GTE_TPT");
  EXPECT_STREQ(toString(Status::ERROR_TFREQD_LT1), "ERROR_TFREQD_LT1");
  EXPECT_STREQ(toString(Status::ERROR_TASK_EXECUTION_FAIL), "ERROR_TASK_EXECUTION_FAIL");
  EXPECT_STREQ(toString(Status::ERROR_THREAD_LAUNCH_FAIL), "ERROR_THREAD_LAUNCH_FAIL");
}

/** @test toString returns correct string for multi-thread errors. */
TEST(SchedulerStatusTest, ToStringMultiThreadErrors) {
  EXPECT_STREQ(toString(Status::ERROR_THREADPOOL_OVERLOAD), "ERROR_THREADPOOL_OVERLOAD");
  EXPECT_STREQ(toString(Status::ERROR_AFFINITY_SETTING_FAIL), "ERROR_AFFINITY_SETTING_FAIL");
  EXPECT_STREQ(toString(Status::ERROR_POLICY_SETTING_FAIL), "ERROR_POLICY_SETTING_FAIL");
}

/** @test toString returns correct string for warnings. */
TEST(SchedulerStatusTest, ToStringWarnings) {
  EXPECT_STREQ(toString(Status::WARN_TASK_STARVATION), "WARN_TASK_STARVATION");
  EXPECT_STREQ(toString(Status::WARN_TASK_NON_SUCCESS_RET), "WARN_TASK_NON_SUCCESS_RET");
  EXPECT_STREQ(toString(Status::WARN_CPU_UNDERUTILIZATION), "WARN_CPU_UNDERUTILIZATION");
  EXPECT_STREQ(toString(Status::WARN_PERIOD_VIOLATION), "WARN_PERIOD_VIOLATION");
}

/** @test toString returns correct string for EOE marker. */
TEST(SchedulerStatusTest, ToStringEOE) {
  EXPECT_STREQ(toString(Status::EOE_SCHEDULER), "EOE_SCHEDULER");
}

/** @test toString returns UNKNOWN_STATUS for invalid value. */
TEST(SchedulerStatusTest, ToStringUnknownValue) {
  // Cast an invalid value to Status
  const auto INVALID = static_cast<Status>(255);
  EXPECT_STREQ(toString(INVALID), "UNKNOWN_STATUS");
}

/* ----------------------------- Type Properties Tests ----------------------------- */

/** @test Status enum fits in uint8_t. */
TEST(SchedulerStatusTest, FitsInUint8) { EXPECT_LE(sizeof(Status), sizeof(std::uint8_t)); }

/** @test All status values are distinct. */
TEST(SchedulerStatusTest, AllValuesDistinct) {
  // Collect all enum values
  std::vector<std::uint8_t> values = {
      static_cast<std::uint8_t>(Status::SUCCESS),
      static_cast<std::uint8_t>(Status::ERROR_TFREQN_GT_FFREQ),
      static_cast<std::uint8_t>(Status::ERROR_FFREQ_MOD_TFREQN_DNE0),
      static_cast<std::uint8_t>(Status::ERROR_OFFSET_GTE_TPT),
      static_cast<std::uint8_t>(Status::ERROR_TFREQD_LT1),
      static_cast<std::uint8_t>(Status::ERROR_TASK_EXECUTION_FAIL),
      static_cast<std::uint8_t>(Status::ERROR_THREAD_LAUNCH_FAIL),
      static_cast<std::uint8_t>(Status::ERROR_THREADPOOL_OVERLOAD),
      static_cast<std::uint8_t>(Status::ERROR_AFFINITY_SETTING_FAIL),
      static_cast<std::uint8_t>(Status::ERROR_POLICY_SETTING_FAIL),
      static_cast<std::uint8_t>(Status::WARN_TASK_STARVATION),
      static_cast<std::uint8_t>(Status::WARN_TASK_NON_SUCCESS_RET),
      static_cast<std::uint8_t>(Status::WARN_CPU_UNDERUTILIZATION),
      static_cast<std::uint8_t>(Status::WARN_PERIOD_VIOLATION),
      static_cast<std::uint8_t>(Status::EOE_SCHEDULER),
  };

  // Check for duplicates
  std::sort(values.begin(), values.end());
  auto last = std::unique(values.begin(), values.end());
  EXPECT_EQ(last, values.end()) << "Duplicate status values detected";
}
