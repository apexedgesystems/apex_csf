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
#include <thread>

using apex::concurrency::applyThreadConfig;
using apex::concurrency::setThreadTimerSlackNs;
using apex::concurrency::ThreadConfig;
using apex::concurrency::test::firstAllowedCpu;
using apex::concurrency::test::ThreadIdentity;

/* ----------------------------- applyThreadConfig Tests ----------------------------- */

/** @test An affinity and a name are applied to the calling thread: it then runs on that one CPU
 * and reports that name, and the call returns true. */
TEST(ThreadConfigTest, AppliesAffinityAndName) {
  const int CPU = firstAllowedCpu();
  ASSERT_GE(CPU, 0);
  ThreadConfig config;
  config.affinity = {static_cast<std::uint8_t>(CPU)};

  bool applied = false;
  ThreadIdentity identity;
  std::thread configured([&]() {
    applied = applyThreadConfig(config, "cfg_probe");
    identity.recordHere();
  });
  configured.join();

  EXPECT_TRUE(applied);
  EXPECT_EQ(identity.allowedCpus, 1);
  EXPECT_EQ(identity.onlyCpu, CPU);
  EXPECT_STREQ(identity.name.data(), "cfg_probe");
}

/** @test A setting the kernel refuses makes the call return false: an affinity naming no CPU of
 * the host, and a scheduling policy that does not exist. The thread keeps running as it was. */
TEST(ThreadConfigTest, ReportsARefusedSetting) {
  ThreadConfig noSuchCpu;
  noSuchCpu.affinity = {255};
  ThreadConfig noSuchPolicy;
  noSuchPolicy.policy = 99;
  noSuchPolicy.priority = 1;

  bool affinityApplied = true;
  bool policyApplied = true;
  ThreadIdentity after;
  ThreadIdentity before;
  std::thread configured([&]() {
    before.recordHere();
    affinityApplied = applyThreadConfig(noSuchCpu, nullptr);
    policyApplied = applyThreadConfig(noSuchPolicy, nullptr);
    after.recordHere();
  });
  configured.join();

  EXPECT_FALSE(affinityApplied);
  EXPECT_FALSE(policyApplied);
  EXPECT_EQ(after.allowedCpus, before.allowedCpus);
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
  EXPECT_EQ(after.allowedCpus, before.allowedCpus);
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
