/**
 * @file ThreadConfig_uTest.cpp
 * @brief Unit tests for the thread configuration helpers in ThreadConfig.hpp.
 *
 * Notes:
 *  - Each case configures a thread of its own, so the test runner's thread
 *    keeps its affinity, name and timer slack.
 */

#include "src/utilities/concurrency/inc/ThreadConfig.hpp"
#include "src/utilities/concurrency/utst/support/ThreadProbe.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <thread>

using apex::concurrency::applyThreadConfig;
using apex::concurrency::setThreadTimerSlackNs;
using apex::concurrency::ThreadConfig;
using apex::concurrency::test::nameableAbsentCpu;
using apex::concurrency::test::nameableAllowedCpu;
using apex::concurrency::test::ThreadIdentity;

/* ----------------------------- applyThreadConfig Tests ----------------------------- */

/** @test An affinity and a name are applied to the calling thread: it then runs on that one CPU
 * and reports that name, and the call returns true. On a host where the thread may run only on
 * CPUs the affinity field cannot name, the name half runs and the case reports a skip. */
TEST(ThreadConfigTest, AppliesAffinityAndName) {
  const std::optional<std::uint8_t> CPU = nameableAllowedCpu();
  ThreadConfig config;
  if (CPU) {
    config.affinity = {*CPU};
  }

  bool applied = false;
  ThreadIdentity identity;
  std::thread configured([&]() {
    applied = applyThreadConfig(config, "cfg_probe");
    identity.recordHere();
  });
  configured.join();

  EXPECT_TRUE(applied);
  EXPECT_STREQ(identity.name.data(), "cfg_probe");
  if (!CPU) {
    GTEST_SKIP() << "no allowed CPU below 256: the affinity half did not run";
  }
  EXPECT_EQ(identity.allowedCpus, 1);
  EXPECT_EQ(identity.onlyCpu, int{*CPU});
}

/** @test A setting the kernel refuses makes the call return false and leaves the thread's CPUs as
 * they were: a scheduling policy that does not exist, and an affinity naming only a CPU number
 * the host cannot have. Where every nameable CPU number is possible on the host, the policy half
 * runs and the case reports a skip. */
TEST(ThreadConfigTest, ReportsARefusedSetting) {
  const std::optional<std::uint8_t> ABSENT_CPU = nameableAbsentCpu();
  ThreadConfig noSuchCpu;
  if (ABSENT_CPU) {
    noSuchCpu.affinity = {*ABSENT_CPU};
  }
  ThreadConfig noSuchPolicy;
  noSuchPolicy.policy = 99;
  noSuchPolicy.priority = 1;

  bool affinityApplied = true;
  bool policyApplied = true;
  ThreadIdentity before;
  ThreadIdentity afterPolicy;
  ThreadIdentity afterAffinity;
  std::thread configured([&]() {
    before.recordHere();
    policyApplied = applyThreadConfig(noSuchPolicy, nullptr);
    afterPolicy.recordHere();
    if (ABSENT_CPU) {
      affinityApplied = applyThreadConfig(noSuchCpu, nullptr);
    }
    afterAffinity.recordHere();
  });
  configured.join();

  EXPECT_FALSE(policyApplied);
  EXPECT_TRUE(afterPolicy.sameCpusAs(before));
  if (!ABSENT_CPU) {
    GTEST_SKIP() << "every CPU number below 256 is possible here: the affinity half did not run";
  }
  EXPECT_FALSE(affinityApplied);
  EXPECT_TRUE(afterAffinity.sameCpusAs(before));
}

/** @test A thread name longer than the kernel takes, fifteen characters, makes the call return
 * false and leaves the thread's name as it was. */
TEST(ThreadConfigTest, ReportsANameTheKernelRefuses) {
  bool applied = true;
  ThreadIdentity before;
  ThreadIdentity after;
  std::thread named([&]() {
    before.recordHere();
    applied = applyThreadConfig(ThreadConfig{}, "sixteen-letters-");
    after.recordHere();
  });
  named.join();

  EXPECT_FALSE(applied);
  EXPECT_STREQ(after.name.data(), before.name.data());
}

/** @test An empty configuration changes nothing and succeeds. */
TEST(ThreadConfigTest, EmptyConfigurationIsANoOp) {
  bool applied = false;
  ThreadIdentity before;
  ThreadIdentity after;
  std::thread configured([&]() {
    before.recordHere();
    applied = applyThreadConfig(ThreadConfig{}, nullptr);
    after.recordHere();
  });
  configured.join();

  EXPECT_TRUE(applied);
  EXPECT_TRUE(after.sameCpusAs(before));
  EXPECT_STREQ(after.name.data(), before.name.data());
}

/* ----------------------------- setThreadTimerSlackNs Tests ----------------------------- */

/** @test The timer slack setter returns the slack it replaced: a first call reports the thread's
 * default, and a second reports the value the first one set. */
TEST(ThreadConfigTest, TimerSlackSetterReturnsThePreviousValue) {
  long first = -2;
  long second = -2;
  long third = -2;
  std::thread configured([&]() {
    first = setThreadTimerSlackNs(1);
    second = setThreadTimerSlackNs(2000);
    third = setThreadTimerSlackNs(0);
  });
  configured.join();

#if defined(__linux__)
  EXPECT_GT(first, 0) << "the thread's default slack";
  EXPECT_EQ(second, 1);
  EXPECT_EQ(third, 2000);
#else
  EXPECT_EQ(first, -1);
  EXPECT_EQ(second, -1);
  EXPECT_EQ(third, -1);
#endif
}
