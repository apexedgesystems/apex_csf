/**
 * @file DataTransform_uTest.cpp
 * @brief Unit tests for DataTransform support component.
 */

#include "src/system/core/support/data_transform/inc/DataTransform.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <vector>

using system_core::data::DataCategory;
using system_core::support::DataTransform;
using system_core::support::DataTransformOpcode;
using system_core::support::DataTransformTlm;
using system_core::support::ResolvedData;
using system_core::support::TRANSFORM_MAX_ENTRIES;
using system_core::support::TransformStats;

/* ----------------------------- Test Resolver ----------------------------- */

namespace {

struct ResolverCtx {
  std::array<std::uint8_t, 64> block{};
  std::uint32_t expectedUid{0x007800};
};

ResolvedData testResolver(void* ctx, std::uint32_t fullUid, DataCategory /*category*/) noexcept {
  auto* rc = static_cast<ResolverCtx*>(ctx);
  if (fullUid != rc->expectedUid) {
    return {};
  }
  return {rc->block.data(), rc->block.size()};
}

/// Helper to send a command with a single-byte payload.
std::uint8_t sendCmd(DataTransform& dt, DataTransformOpcode op, std::uint8_t index) {
  std::vector<std::uint8_t> response;
  std::array<std::uint8_t, 1> payload = {index};
  return dt.handleCommand(static_cast<std::uint16_t>(op), {payload.data(), payload.size()},
                          response);
}

/// Helper to send APPLY_ENTRY command.
std::uint8_t applyEntry(DataTransform& dt, std::uint8_t index) {
  return sendCmd(dt, DataTransformOpcode::APPLY_ENTRY, index);
}

/// Helper to send APPLY_ALL command.
std::uint8_t applyAll(DataTransform& dt) {
  std::vector<std::uint8_t> response;
  return dt.handleCommand(static_cast<std::uint16_t>(DataTransformOpcode::APPLY_ALL), {}, response);
}

} // namespace

/* ----------------------------- Default Construction ----------------------------- */

/** @test DataTransform starts with all entries disarmed and zero stats. */
TEST(DataTransform, DefaultConstruction) {
  DataTransform dt;
  EXPECT_EQ(dt.componentId(), 202);
  EXPECT_STREQ(dt.componentName(), "DataTransform");

  const auto& STATS = dt.stats();
  EXPECT_EQ(STATS.applyCycles, 0u);
  EXPECT_EQ(STATS.masksApplied, 0u);
  EXPECT_EQ(STATS.resolveFailures, 0u);
  EXPECT_EQ(STATS.applyFailures, 0u);
  EXPECT_EQ(STATS.entriesArmed, 0u);

  for (std::size_t i = 0; i < TRANSFORM_MAX_ENTRIES; ++i) {
    EXPECT_FALSE(dt.entries()[i].armed);
    EXPECT_TRUE(dt.entries()[i].proxy.empty());
  }
}

/* ----------------------------- Init ----------------------------- */

/** @test Init fails without resolver. */
TEST(DataTransform, InitFailsWithoutResolver) {
  DataTransform dt;
  auto status = dt.init();
  EXPECT_NE(status, 0u);
}

/** @test Init succeeds with resolver. */
TEST(DataTransform, InitSucceedsWithResolver) {
  DataTransform dt;
  ResolverCtx ctx;
  dt.setResolver(testResolver, &ctx);
  auto status = dt.init();
  EXPECT_EQ(status, 0u);
}

/* ----------------------------- APPLY_ENTRY Command ----------------------------- */

/** @test APPLY_ENTRY on unarmed entry returns error. */
TEST(DataTransform, ApplyEntryUnarmed) {
  DataTransform dt;
  ResolverCtx ctx;
  dt.setResolver(testResolver, &ctx);
  (void)dt.init();

  auto status = applyEntry(dt, 0);
  EXPECT_NE(status, 0u);
}

/** @test APPLY_ENTRY with armed entry and mask zeros target bytes. */
TEST(DataTransform, ApplyEntryZeroMask) {
  DataTransform dt;
  ResolverCtx ctx;
  ctx.block.fill(0xFF);
  dt.setResolver(testResolver, &ctx);
  (void)dt.init();

  auto* e = dt.entry(0);
  ASSERT_NE(e, nullptr);
  e->target = {0x007800, DataCategory::OUTPUT, 4, 4};
  e->armed = true;
  (void)e->proxy.pushZeroMask(0, 4);

  auto status = applyEntry(dt, 0);
  EXPECT_EQ(status, 0u);
  EXPECT_EQ(dt.stats().masksApplied, 1u);

  EXPECT_EQ(ctx.block[3], 0xFF);
  EXPECT_EQ(ctx.block[4], 0x00);
  EXPECT_EQ(ctx.block[5], 0x00);
  EXPECT_EQ(ctx.block[6], 0x00);
  EXPECT_EQ(ctx.block[7], 0x00);
  EXPECT_EQ(ctx.block[8], 0xFF);
}

/** @test APPLY_ENTRY with resolve failure increments failure counter. */
TEST(DataTransform, ApplyEntryResolveFailure) {
  DataTransform dt;
  ResolverCtx ctx;
  ctx.expectedUid = 0x007800;
  dt.setResolver(testResolver, &ctx);
  (void)dt.init();

  auto* e = dt.entry(0);
  e->target = {0xDEAD00, DataCategory::OUTPUT, 0, 4};
  e->armed = true;
  (void)e->proxy.pushZeroMask(0, 4);

  applyEntry(dt, 0);
  EXPECT_EQ(dt.stats().resolveFailures, 1u);
  EXPECT_EQ(dt.stats().masksApplied, 0u);
}

/** @test APPLY_ENTRY with armed but empty proxy does not apply. */
TEST(DataTransform, ApplyEntryEmptyProxy) {
  DataTransform dt;
  ResolverCtx ctx;
  dt.setResolver(testResolver, &ctx);
  (void)dt.init();

  auto* e = dt.entry(0);
  e->target = {0x007800, DataCategory::OUTPUT, 0, 4};
  e->armed = true;

  auto status = applyEntry(dt, 0);
  EXPECT_NE(status, 0u);
  EXPECT_EQ(dt.stats().masksApplied, 0u);
}

/** @test APPLY_ENTRY out of bounds returns error. */
TEST(DataTransform, ApplyEntryOutOfBounds) {
  DataTransform dt;
  ResolverCtx ctx;
  dt.setResolver(testResolver, &ctx);
  (void)dt.init();

  auto status = applyEntry(dt, static_cast<std::uint8_t>(TRANSFORM_MAX_ENTRIES));
  EXPECT_NE(status, 0u);
}

/* ----------------------------- APPLY_ALL Command ----------------------------- */

/** @test APPLY_ALL applies all armed entries. */
TEST(DataTransform, ApplyAllMultipleEntries) {
  DataTransform dt;
  ResolverCtx ctx;
  ctx.block.fill(0xFF);
  dt.setResolver(testResolver, &ctx);
  (void)dt.init();

  // Entry 0: zero bytes 0-1
  auto* e0 = dt.entry(0);
  e0->target = {0x007800, DataCategory::OUTPUT, 0, 2};
  e0->armed = true;
  (void)e0->proxy.pushZeroMask(0, 2);

  // Entry 1: flip bytes 8-9
  auto* e1 = dt.entry(1);
  e1->target = {0x007800, DataCategory::OUTPUT, 8, 2};
  e1->armed = true;
  (void)e1->proxy.pushFlipMask(0, 2);

  auto status = applyAll(dt);
  EXPECT_EQ(status, 0u);
  EXPECT_EQ(dt.stats().masksApplied, 2u);

  EXPECT_EQ(ctx.block[0], 0x00);
  EXPECT_EQ(ctx.block[1], 0x00);
  EXPECT_EQ(ctx.block[2], 0xFF);
  EXPECT_EQ(ctx.block[8], 0x00);
  EXPECT_EQ(ctx.block[9], 0x00);
}

/** @test APPLY_ALL with no armed entries succeeds with zero applications. */
TEST(DataTransform, ApplyAllNoArmedEntries) {
  DataTransform dt;
  ResolverCtx ctx;
  dt.setResolver(testResolver, &ctx);
  (void)dt.init();

  auto status = applyAll(dt);
  EXPECT_EQ(status, 0u);
  EXPECT_EQ(dt.stats().applyCycles, 1u);
  EXPECT_EQ(dt.stats().masksApplied, 0u);
}

/* ----------------------------- Command Interface ----------------------------- */

/** @test ARM_ENTRY command arms an entry. */
TEST(DataTransform, CommandArmEntry) {
  DataTransform dt;
  ResolverCtx ctx;
  dt.setResolver(testResolver, &ctx);
  (void)dt.init();

  auto status = sendCmd(dt, DataTransformOpcode::ARM_ENTRY, 2);
  EXPECT_EQ(status, 0u);
  EXPECT_TRUE(dt.entries()[2].armed);
}

/** @test DISARM_ENTRY command disarms an entry. */
TEST(DataTransform, CommandDisarmEntry) {
  DataTransform dt;
  ResolverCtx ctx;
  dt.setResolver(testResolver, &ctx);
  (void)dt.init();

  dt.entry(3)->armed = true;
  auto status = sendCmd(dt, DataTransformOpcode::DISARM_ENTRY, 3);
  EXPECT_EQ(status, 0u);
  EXPECT_FALSE(dt.entries()[3].armed);
}

/** @test ARM_ENTRY with out-of-bounds index returns error. */
TEST(DataTransform, CommandArmEntryOutOfBounds) {
  DataTransform dt;
  ResolverCtx ctx;
  dt.setResolver(testResolver, &ctx);
  (void)dt.init();

  auto status =
      sendCmd(dt, DataTransformOpcode::ARM_ENTRY, static_cast<std::uint8_t>(TRANSFORM_MAX_ENTRIES));
  EXPECT_NE(status, 0u);
}

/** @test PUSH_ZERO_MASK pushes mask to entry proxy. */
TEST(DataTransform, CommandPushZeroMask) {
  DataTransform dt;
  ResolverCtx ctx;
  dt.setResolver(testResolver, &ctx);
  (void)dt.init();

  std::vector<std::uint8_t> response;
  std::array<std::uint8_t, 4> payload = {0, 4, 0, 2};

  auto status = dt.handleCommand(static_cast<std::uint16_t>(DataTransformOpcode::PUSH_ZERO_MASK),
                                 {payload.data(), payload.size()}, response);
  EXPECT_EQ(status, 0u);
  EXPECT_EQ(dt.entries()[0].proxy.size(), 1u);
}

/** @test CLEAR_MASKS clears an entry's proxy queue. */
TEST(DataTransform, CommandClearMasks) {
  DataTransform dt;
  ResolverCtx ctx;
  dt.setResolver(testResolver, &ctx);
  (void)dt.init();

  (void)dt.entry(1)->proxy.pushZeroMask(0, 4);
  (void)dt.entry(1)->proxy.pushHighMask(0, 4);
  EXPECT_EQ(dt.entries()[1].proxy.size(), 2u);

  auto status = sendCmd(dt, DataTransformOpcode::CLEAR_MASKS, 1);
  EXPECT_EQ(status, 0u);
  EXPECT_EQ(dt.entries()[1].proxy.size(), 0u);
}

/** @test CLEAR_ALL disarms all entries and clears all masks. */
TEST(DataTransform, CommandClearAll) {
  DataTransform dt;
  ResolverCtx ctx;
  dt.setResolver(testResolver, &ctx);
  (void)dt.init();

  dt.entry(0)->armed = true;
  dt.entry(3)->armed = true;
  (void)dt.entry(0)->proxy.pushZeroMask(0, 4);
  (void)dt.entry(3)->proxy.pushHighMask(0, 2);

  std::vector<std::uint8_t> response;
  auto status =
      dt.handleCommand(static_cast<std::uint16_t>(DataTransformOpcode::CLEAR_ALL), {}, response);
  EXPECT_EQ(status, 0u);

  for (std::size_t i = 0; i < TRANSFORM_MAX_ENTRIES; ++i) {
    EXPECT_FALSE(dt.entries()[i].armed);
    EXPECT_TRUE(dt.entries()[i].proxy.empty());
  }
}

/** @test GET_STATS returns valid telemetry. */
TEST(DataTransform, CommandGetStats) {
  DataTransform dt;
  ResolverCtx ctx;
  ctx.block.fill(0xFF);
  dt.setResolver(testResolver, &ctx);
  (void)dt.init();

  // Do some work so stats are non-zero
  auto* e = dt.entry(0);
  e->target = {0x007800, DataCategory::OUTPUT, 0, 4};
  e->armed = true;
  (void)e->proxy.pushZeroMask(0, 4);
  applyEntry(dt, 0);

  std::vector<std::uint8_t> response;
  auto status =
      dt.handleCommand(static_cast<std::uint16_t>(DataTransformOpcode::GET_STATS), {}, response);
  EXPECT_EQ(status, 0u);
  EXPECT_EQ(response.size(), sizeof(DataTransformTlm));

  DataTransformTlm tlm{};
  std::memcpy(&tlm, response.data(), sizeof(tlm));
  EXPECT_EQ(tlm.applyCycles, 1u);
  EXPECT_EQ(tlm.masksApplied, 1u);
}

/* ----------------------------- Reset ----------------------------- */

/** @test Reset clears all entries and stats. */
TEST(DataTransform, ResetClearsState) {
  DataTransform dt;
  ResolverCtx ctx;
  ctx.block.fill(0xFF);
  dt.setResolver(testResolver, &ctx);
  (void)dt.init();

  auto* e = dt.entry(0);
  e->target = {0x007800, DataCategory::OUTPUT, 0, 4};
  e->armed = true;
  (void)e->proxy.pushZeroMask(0, 4);
  applyEntry(dt, 0);

  EXPECT_GT(dt.stats().applyCycles, 0u);

  dt.reset();

  EXPECT_EQ(dt.stats().applyCycles, 0u);
  EXPECT_EQ(dt.stats().masksApplied, 0u);
  EXPECT_FALSE(dt.entries()[0].armed);
  EXPECT_TRUE(dt.entries()[0].proxy.empty());
}

/* ----------------------------- Entry Access ----------------------------- */

/** @test Entry accessor returns nullptr for out-of-bounds index. */
TEST(DataTransform, EntryOutOfBoundsReturnsNull) {
  DataTransform dt;
  EXPECT_EQ(dt.entry(TRANSFORM_MAX_ENTRIES), nullptr);
  EXPECT_EQ(dt.entry(255), nullptr);
}

/** @test Entry accessor returns valid pointer for in-bounds index. */
TEST(DataTransform, EntryInBoundsReturnsValid) {
  DataTransform dt;
  for (std::size_t i = 0; i < TRANSFORM_MAX_ENTRIES; ++i) {
    EXPECT_NE(dt.entry(i), nullptr);
  }
}

/* ----------------------------- Full Fault Injection Flow ----------------------------- */

/** @test Simulates the action engine sequence: arm, push mask, apply, disarm. */
TEST(DataTransform, FullFaultInjectionFlow) {
  DataTransform dt;
  ResolverCtx ctx;
  ctx.block.fill(0xAA);
  dt.setResolver(testResolver, &ctx);
  (void)dt.init();

  // Pre-configure entry target (would come from TPRM in production)
  auto* e = dt.entry(0);
  e->target = {0x007800, DataCategory::OUTPUT, 0, 4};

  // Sequence step 1: ARM_ENTRY(0)
  EXPECT_EQ(sendCmd(dt, DataTransformOpcode::ARM_ENTRY, 0), 0u);
  EXPECT_TRUE(dt.entries()[0].armed);

  // Sequence step 2: PUSH_ZERO_MASK(0, offset=0, len=4)
  {
    std::vector<std::uint8_t> response;
    std::array<std::uint8_t, 4> payload = {0, 0, 0, 4};
    EXPECT_EQ(dt.handleCommand(static_cast<std::uint16_t>(DataTransformOpcode::PUSH_ZERO_MASK),
                               {payload.data(), payload.size()}, response),
              0u);
  }

  // Sequence step 3: APPLY_ENTRY(0) — this is the actual mutation
  EXPECT_EQ(applyEntry(dt, 0), 0u);

  // Verify bytes were zeroed
  EXPECT_EQ(ctx.block[0], 0x00);
  EXPECT_EQ(ctx.block[1], 0x00);
  EXPECT_EQ(ctx.block[2], 0x00);
  EXPECT_EQ(ctx.block[3], 0x00);
  EXPECT_EQ(ctx.block[4], 0xAA); // Unchanged

  // Sequence step 4: DISARM_ENTRY(0) — fault injection complete
  EXPECT_EQ(sendCmd(dt, DataTransformOpcode::DISARM_ENTRY, 0), 0u);
  EXPECT_FALSE(dt.entries()[0].armed);

  // Further APPLY_ENTRY should fail (disarmed)
  EXPECT_NE(applyEntry(dt, 0), 0u);
  EXPECT_EQ(dt.stats().masksApplied, 1u); // Still 1 from before
}
