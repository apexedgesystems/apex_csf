/**
 * @file TestPlugin_uTest.cpp
 * @brief Unit tests for the HIL demo's hot-swap test plugin.
 *
 * Covers the plugin's two observations as a client reaches them:
 *   - The registered output block, copied the way INSPECT copies it.
 *   - GET_STATS, through handleCommand().
 *
 * The concurrent case runs both while the task is being called from
 * another thread. Under ThreadSanitizer it fails on any access to the
 * plugin's data that is not synchronized with the task.
 */

#define APEX_TEST_PLUGIN_VERSION 2

#include "demos/apex_hil_demo/test/plugin/TestPlugin.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

using system_core::data::DataCategory;
using test::TestPlugin;

namespace {

/// The registered OUTPUT block's bytes, copied as INSPECT copies them.
std::vector<std::uint8_t> inspectOutput(const TestPlugin& plugin) {
  for (std::size_t i = 0; i < plugin.dataCount(); ++i) {
    const auto* desc = plugin.dataDescriptor(i);
    if (desc->category == DataCategory::OUTPUT) {
      std::vector<std::uint8_t> bytes(desc->size);
      std::memcpy(bytes.data(), desc->ptr, desc->size);
      return bytes;
    }
  }
  return {};
}

/// Brings a plugin up the way the loader does: TPRM ingest, then init.
bool initialize(TestPlugin& plugin) {
  static_cast<void>(plugin.loadTprm({}));
  return plugin.init() == 0;
}

TestPlugin::Stats getStats(TestPlugin& plugin) {
  std::vector<std::uint8_t> response;
  TestPlugin::Stats stats{};
  stats.version = 0;
  if (plugin.handleCommand(TestPlugin::GET_STATS, {}, response) == 0 &&
      response.size() == sizeof(stats)) {
    std::memcpy(&stats, response.data(), sizeof(stats));
  }
  return stats;
}

} // namespace

/** @test The registered output block is the build's version and nothing else. */
TEST(TestPluginTest, RegisteredOutputIsTheBuildVersion) {
  TestPlugin plugin;
  ASSERT_TRUE(initialize(plugin));

  const auto BYTES = inspectOutput(plugin);
  ASSERT_EQ(BYTES.size(), sizeof(std::uint32_t));
  std::uint32_t version = 0;
  std::memcpy(&version, BYTES.data(), sizeof(version));
  EXPECT_EQ(version, 2U);
}

/** @test GET_STATS returns the version and the number of task runs. */
TEST(TestPluginTest, GetStatsCountsTaskRuns) {
  TestPlugin plugin;
  ASSERT_TRUE(initialize(plugin));

  EXPECT_EQ(getStats(plugin).version, 2U);
  EXPECT_EQ(getStats(plugin).tickCount, 0U);

  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(plugin.tick(), 0);
  }
  EXPECT_EQ(getStats(plugin).tickCount, 3U);
}

/** @test An opcode the plugin does not define is the base class's to answer. */
TEST(TestPluginTest, OtherOpcodesReachTheBaseClass) {
  TestPlugin plugin;
  ASSERT_TRUE(initialize(plugin));

  std::vector<std::uint8_t> response;
  EXPECT_NE(plugin.handleCommand(0x01FF, {}, response), 0);
  EXPECT_TRUE(response.empty());
}

/** @test Both observations hold while the task runs on another thread. */
TEST(TestPluginTest, ObservationsHoldWhileTheTaskRuns) {
  constexpr std::uint32_t RUNS = 200000;
  TestPlugin plugin;
  ASSERT_TRUE(initialize(plugin));

  std::atomic<bool> go{false};
  std::thread task([&] {
    while (!go.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    for (std::uint32_t i = 0; i < RUNS; ++i) {
      static_cast<void>(plugin.tick());
    }
  });

  go.store(true, std::memory_order_release);
  std::uint32_t last = 0;
  std::uint32_t reads = 0;
  bool ordered = true;
  bool versioned = true;
  while (last < RUNS) {
    const auto BYTES = inspectOutput(plugin);
    std::uint32_t version = 0;
    if (BYTES.size() == sizeof(version)) {
      std::memcpy(&version, BYTES.data(), sizeof(version));
    }
    const TestPlugin::Stats STATS = getStats(plugin);
    versioned = versioned && version == 2U && STATS.version == 2U;
    ordered = ordered && STATS.tickCount >= last && STATS.tickCount <= RUNS;
    last = STATS.tickCount;
    ++reads;
  }
  task.join();

  EXPECT_TRUE(versioned);
  EXPECT_TRUE(ordered);
  EXPECT_GT(reads, 0U);
  EXPECT_EQ(getStats(plugin).tickCount, RUNS);
}
