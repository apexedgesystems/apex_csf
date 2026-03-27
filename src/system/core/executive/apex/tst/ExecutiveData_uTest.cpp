/**
 * @file ExecutiveData_uTest.cpp
 * @brief Unit tests for ExecutiveTunableParams and ThreadConfigEntry.
 */

#include "src/system/core/executive/apex/inc/ExecutiveData.hpp"

#include <cstring>

#include <gtest/gtest.h>

/* ----------------------------- ExecutiveTunableParams Tests ----------------------------- */

/** @test ExecutiveTunableParams has correct size for binary serialization. */
TEST(ExecutiveTunableParams, SizeIs48Bytes) {
  EXPECT_EQ(sizeof(executive::ExecutiveTunableParams), 48U);
}

/** @test ExecutiveTunableParams is trivially copyable. */
TEST(ExecutiveTunableParams, IsTriviallyCopyable) {
  EXPECT_TRUE(std::is_trivially_copyable_v<executive::ExecutiveTunableParams>);
}

/** @test ExecutiveTunableParams default values. */
TEST(ExecutiveTunableParams, DefaultValues) {
  const executive::ExecutiveTunableParams params{};

  EXPECT_EQ(params.clockFrequencyHz, 100);
  EXPECT_EQ(params.rtMode, 0); // HARD_TICK_COMPLETE
  EXPECT_EQ(params.rtMaxLagTicks, 10);
  EXPECT_EQ(params.startupMode, 0); // AUTO
  EXPECT_EQ(params.startupDelaySeconds, 1);
  EXPECT_EQ(params.shutdownMode, 0); // SIGNAL_ONLY
  EXPECT_EQ(params.skipCleanup, 0);
  EXPECT_EQ(params.shutdownAfterSeconds, 0);
  EXPECT_EQ(params.shutdownAtCycle, 0);
  EXPECT_EQ(params.watchdogIntervalMs, 1000);
  EXPECT_EQ(params.profilingSampleEveryN, 0);
}

/** @test ExecutiveTunableParams can be memcpy'd. */
TEST(ExecutiveTunableParams, CanBeMemcopied) {
  executive::ExecutiveTunableParams src{};
  src.clockFrequencyHz = 200;
  src.rtMode = 2;
  src.watchdogIntervalMs = 500;

  executive::ExecutiveTunableParams dst{};
  std::memcpy(&dst, &src, sizeof(executive::ExecutiveTunableParams));

  EXPECT_EQ(dst.clockFrequencyHz, 200);
  EXPECT_EQ(dst.rtMode, 2);
  EXPECT_EQ(dst.watchdogIntervalMs, 500);
}

/* ----------------------------- ThreadConfigEntry Tests ----------------------------- */

/** @test ThreadConfigEntry has correct size. */
TEST(ThreadConfigEntry, SizeIs11Bytes) { EXPECT_EQ(sizeof(executive::ThreadConfigEntry), 11U); }

/** @test ExecutiveThreadConfigTprm has correct size. */
TEST(ExecutiveThreadConfigTprm, SizeIs66Bytes) {
  EXPECT_EQ(sizeof(executive::ExecutiveThreadConfigTprm), 66U);
}

/** @test MAX_AFFINITY_CORES constant. */
TEST(ThreadConfigEntry, MaxAffinityCoresConstant) { EXPECT_EQ(executive::MAX_AFFINITY_CORES, 8); }

/** @test AFFINITY_UNUSED constant. */
TEST(ThreadConfigEntry, AffinityUnusedConstant) { EXPECT_EQ(executive::AFFINITY_UNUSED, 0xFF); }

/* ----------------------------- threadConfigFromTprm Tests ----------------------------- */

/** @test threadConfigFromTprm copies policy and priority. */
TEST(ThreadConfigFromTprm, CopiesPolicyAndPriority) {
  executive::ThreadConfigEntry entry{};
  entry.policy = 1; // FIFO
  entry.priority = 50;
  entry.affinityCount = 0;

  executive::PrimaryThreadConfig out{};
  executive::threadConfigFromTprm(entry, out);

  EXPECT_EQ(out.policy, 1);
  EXPECT_EQ(out.priority, 50);
  EXPECT_TRUE(out.affinity.empty());
}

/** @test threadConfigFromTprm parses affinity cores. */
TEST(ThreadConfigFromTprm, ParsesAffinityCores) {
  executive::ThreadConfigEntry entry{};
  entry.policy = 0;
  entry.priority = 0;
  entry.affinityCount = 3;
  entry.affinity[0] = 0;
  entry.affinity[1] = 2;
  entry.affinity[2] = 4;
  for (std::uint8_t i = 3; i < executive::MAX_AFFINITY_CORES; ++i) {
    entry.affinity[i] = executive::AFFINITY_UNUSED;
  }

  executive::PrimaryThreadConfig out{};
  executive::threadConfigFromTprm(entry, out);

  ASSERT_EQ(out.affinity.size(), 3U);
  EXPECT_EQ(out.affinity[0], 0);
  EXPECT_EQ(out.affinity[1], 2);
  EXPECT_EQ(out.affinity[2], 4);
}

/** @test threadConfigFromTprm skips AFFINITY_UNUSED values. */
TEST(ThreadConfigFromTprm, SkipsUnusedAffinitySlots) {
  executive::ThreadConfigEntry entry{};
  entry.policy = 0;
  entry.priority = 0;
  entry.affinityCount = 4;
  entry.affinity[0] = 1;
  entry.affinity[1] = executive::AFFINITY_UNUSED; // Skipped
  entry.affinity[2] = 3;
  entry.affinity[3] = executive::AFFINITY_UNUSED; // Skipped

  executive::PrimaryThreadConfig out{};
  executive::threadConfigFromTprm(entry, out);

  // Only valid (non-UNUSED) cores should be added
  ASSERT_EQ(out.affinity.size(), 2U);
  EXPECT_EQ(out.affinity[0], 1);
  EXPECT_EQ(out.affinity[1], 3);
}

/** @test threadConfigFromTprm respects affinityCount limit. */
TEST(ThreadConfigFromTprm, RespectsAffinityCountLimit) {
  executive::ThreadConfigEntry entry{};
  entry.policy = 0;
  entry.priority = 0;
  entry.affinityCount = 2; // Only first 2 slots
  entry.affinity[0] = 0;
  entry.affinity[1] = 1;
  entry.affinity[2] = 2; // Should be ignored
  entry.affinity[3] = 3; // Should be ignored

  executive::PrimaryThreadConfig out{};
  executive::threadConfigFromTprm(entry, out);

  ASSERT_EQ(out.affinity.size(), 2U);
  EXPECT_EQ(out.affinity[0], 0);
  EXPECT_EQ(out.affinity[1], 1);
}

/** @test threadConfigFromTprm sets stackSize to 0. */
TEST(ThreadConfigFromTprm, StackSizeIsZero) {
  executive::ThreadConfigEntry entry{};
  entry.policy = 0;
  entry.priority = 0;
  entry.affinityCount = 0;

  executive::PrimaryThreadConfig out{};
  out.stackSize = 12345; // Pre-set value
  executive::threadConfigFromTprm(entry, out);

  EXPECT_EQ(out.stackSize, 0U);
}
