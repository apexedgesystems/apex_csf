/**
 * @file DataTransform_uTest.cpp
 * @brief Unit tests for DataTransform support component.
 */

#include "src/system/core/support/data_transform/inc/DataTransform.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unistd.h>
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

/* ----------------------------- Held Masks ----------------------------- */

/// Arm entry 0 on the test block, queue a zero mask over its first four bytes and apply it.
void armApplyZero(DataTransform& dt, ResolverCtx& ctx) {
  ctx.block.fill(0xAA);
  dt.setResolver(testResolver, &ctx);
  (void)dt.init();
  dt.entry(0)->target = {0x007800, DataCategory::OUTPUT, 0, 4};
  ASSERT_EQ(sendCmd(dt, DataTransformOpcode::ARM_ENTRY, 0), 0u);
  std::vector<std::uint8_t> response;
  std::array<std::uint8_t, 4> payload = {0, 0, 0, 4};
  ASSERT_EQ(dt.handleCommand(static_cast<std::uint16_t>(DataTransformOpcode::PUSH_ZERO_MASK),
                             {payload.data(), payload.size()}, response),
            0u);
  ASSERT_EQ(applyEntry(dt, 0), 0u);
  ASSERT_EQ(ctx.block[0], 0x00);
}

/** @test A hold tick re-applies the applied mask after the block's owner rewrote the bytes. */
TEST(DataTransform, HoldReappliesTheAppliedMaskAfterTheOwnerRewritesTheBlock) {
  DataTransform dt;
  ResolverCtx ctx;
  armApplyZero(dt, ctx);

  // The owner's step writes a fresh value over the masked bytes.
  ctx.block[0] = 0x12;
  ctx.block[3] = 0x34;
  EXPECT_EQ(dt.hold(), 0u);
  EXPECT_EQ(ctx.block[0], 0x00);
  EXPECT_EQ(ctx.block[3], 0x00);
  EXPECT_EQ(ctx.block[4], 0xAA); // Outside the mask, untouched
  // The hold is not a commanded apply.
  EXPECT_EQ(dt.stats().masksApplied, 1u);
  EXPECT_EQ(dt.stats().applyFailures, 0u);
}

/** @test An armed mask that no command has applied is not written by a hold tick. */
TEST(DataTransform, HoldLeavesAnUnappliedMaskAlone) {
  DataTransform dt;
  ResolverCtx ctx;
  ctx.block.fill(0xAA);
  dt.setResolver(testResolver, &ctx);
  (void)dt.init();
  dt.entry(0)->target = {0x007800, DataCategory::OUTPUT, 0, 4};
  ASSERT_EQ(sendCmd(dt, DataTransformOpcode::ARM_ENTRY, 0), 0u);
  std::vector<std::uint8_t> response;
  std::array<std::uint8_t, 4> payload = {0, 0, 0, 4};
  ASSERT_EQ(dt.handleCommand(static_cast<std::uint16_t>(DataTransformOpcode::PUSH_ZERO_MASK),
                             {payload.data(), payload.size()}, response),
            0u);
  EXPECT_EQ(dt.hold(), 0u);
  EXPECT_EQ(ctx.block[0], 0xAA);
}

/** @test DISARM_ENTRY, CLEAR_MASKS and CLEAR_ALL each end the hold. */
TEST(DataTransform, DisarmAndClearEndTheHold) {
  for (const auto OP : {DataTransformOpcode::DISARM_ENTRY, DataTransformOpcode::CLEAR_MASKS,
                        DataTransformOpcode::CLEAR_ALL}) {
    DataTransform dt;
    ResolverCtx ctx;
    armApplyZero(dt, ctx);
    std::vector<std::uint8_t> response;
    std::array<std::uint8_t, 1> index = {0};
    if (OP == DataTransformOpcode::CLEAR_ALL) {
      ASSERT_EQ(dt.handleCommand(static_cast<std::uint16_t>(OP), {}, response), 0u);
    } else {
      ASSERT_EQ(
          dt.handleCommand(static_cast<std::uint16_t>(OP), {index.data(), index.size()}, response),
          0u);
    }
    ctx.block[0] = 0x12;
    EXPECT_EQ(dt.hold(), 0u);
    EXPECT_EQ(ctx.block[0], 0x12) << "opcode " << static_cast<int>(OP);
  }
  // A re-armed entry whose mask is still queued does not hold until a command applies it again.
  DataTransform dt;
  ResolverCtx ctx;
  armApplyZero(dt, ctx);
  ASSERT_EQ(sendCmd(dt, DataTransformOpcode::DISARM_ENTRY, 0), 0u);
  ASSERT_EQ(sendCmd(dt, DataTransformOpcode::ARM_ENTRY, 0), 0u);
  ctx.block[0] = 0x12;
  EXPECT_EQ(dt.hold(), 0u);
  EXPECT_EQ(ctx.block[0], 0x12);
  ASSERT_EQ(applyEntry(dt, 0), 0u);
  ctx.block[0] = 0x12;
  EXPECT_EQ(dt.hold(), 0u);
  EXPECT_EQ(ctx.block[0], 0x00);
}

/* ----------------------------- TPRM Load Contract ----------------------------- */

/** @test Missing tprm is optional: pass-through config, success, configured. */
TEST(DataTransform, LoadTprmMissingFileIsOptional) {
  const auto DIR =
      std::filesystem::temp_directory_path() / ("dtx_tprm_missing_" + std::to_string(getpid()));
  std::filesystem::create_directories(DIR);

  system_core::support::DataTransform dt;
  dt.setInstanceIndex(0);
  EXPECT_EQ(dt.loadTprm(DIR), system_core::system_component::TprmIngest::DEFAULTS);
  EXPECT_TRUE(dt.isConfigured());

  std::filesystem::remove_all(DIR);
}

/* ----------------------------- Hold Ownership (threads) ----------------------------- */

namespace {

/// Marks the thread that runs hold(); the gate resolver treats it differently.
thread_local bool tlsHoldThread = false;

/**
 * @brief A resolver that can park the calling thread inside the resolve.
 *
 * `parkHold` parks the hold thread, `parkCommand` parks a command thread,
 * each until `release()`; `failHold` makes the hold thread's resolve fail.
 * Parking inside the resolve pins the interleaving the tests need: a hold
 * or a command caught mid-write while the other side arrives.
 */
struct GateCtx {
  std::array<std::uint8_t, 64> block{};
  std::atomic<bool> parkHold{false};
  std::atomic<bool> parkCommand{false};
  std::atomic<bool> failHold{false};
  std::atomic<bool> parked{false};
  std::mutex mutex;
  std::condition_variable cv;
  bool released{false};

  void release() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      released = true;
    }
    cv.notify_all();
  }
  bool waitParked() {
    for (int i = 0; i < 5000 && !parked.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return parked.load(std::memory_order_acquire);
  }
};

ResolvedData gateResolver(void* ctx, std::uint32_t fullUid, DataCategory /*category*/) noexcept {
  auto* c = static_cast<GateCtx*>(ctx);
  if (fullUid != 0x007800) {
    return {};
  }
  if (tlsHoldThread && c->failHold.load(std::memory_order_acquire)) {
    return {};
  }
  const bool PARK = tlsHoldThread ? c->parkHold.load(std::memory_order_acquire)
                                  : c->parkCommand.load(std::memory_order_acquire);
  if (PARK) {
    std::unique_lock<std::mutex> lock(c->mutex);
    c->parked.store(true, std::memory_order_release);
    c->cv.wait_for(lock, std::chrono::seconds(10), [c] { return c->released; });
  }
  return {c->block.data(), c->block.size()};
}

/// Arm entry 0 with a 4-byte ZERO mask on the gate resolver's block and apply it.
void armApplyZeroGated(DataTransform& dt, GateCtx& ctx) {
  ctx.block.fill(0xAA);
  dt.setResolver(gateResolver, &ctx);
  (void)dt.init();
  dt.entry(0)->target = {0x007800, DataCategory::OUTPUT, 0, 4};
  ASSERT_EQ(sendCmd(dt, DataTransformOpcode::ARM_ENTRY, 0), 0u);
  std::vector<std::uint8_t> response;
  std::array<std::uint8_t, 4> payload = {0, 0, 0, 4};
  ASSERT_EQ(dt.handleCommand(static_cast<std::uint16_t>(DataTransformOpcode::PUSH_ZERO_MASK),
                             {payload.data(), payload.size()}, response),
            0u);
  ASSERT_EQ(applyEntry(dt, 0), 0u);
  ASSERT_EQ(ctx.block[0], 0x00);
}

} // namespace

/** @test A hold tick that finds a command owning the writes skips the tick and counts it. */
TEST(DataTransformHold, SkipsTheTickWhileACommandOwnsTheWrites) {
  DataTransform dt;
  GateCtx ctx;
  armApplyZeroGated(dt, ctx);
  ctx.block.fill(0xAA);

  // A command thread is inside APPLY_ENTRY, parked in the resolve with the lock held.
  ctx.parkCommand.store(true, std::memory_order_release);
  std::thread command([&] { EXPECT_EQ(applyEntry(dt, 0), 0u); });
  ASSERT_TRUE(ctx.waitParked());

  EXPECT_EQ(dt.hold(), 0u); // returns at once: the command owns the writes
  EXPECT_EQ(dt.holdSkips(), 1u);
  EXPECT_EQ(dt.holdApplies(), 0u);
  EXPECT_EQ(ctx.block[0], 0xAA); // the hold wrote nothing

  ctx.release();
  command.join();
  EXPECT_EQ(ctx.block[0], 0x00); // the command's own apply

  ctx.block[0] = 0x12;
  EXPECT_EQ(dt.hold(), 0u);
  EXPECT_EQ(ctx.block[0], 0x00);
  EXPECT_EQ(dt.holdApplies(), 1u);
  EXPECT_EQ(dt.holdSkips(), 1u);
}

/** @test DISARM_ENTRY, CLEAR_MASKS and CLEAR_ALL return only after an in-progress hold write
 * completes, and no hold writes the entry afterwards. */
TEST(DataTransformHold, WithdrawalWaitsForTheInProgressHoldAndEndsIt) {
  for (const auto OP : {DataTransformOpcode::DISARM_ENTRY, DataTransformOpcode::CLEAR_MASKS,
                        DataTransformOpcode::CLEAR_ALL}) {
    DataTransform dt;
    GateCtx ctx;
    armApplyZeroGated(dt, ctx);
    ctx.block[0] = 0x12; // the owner's rewrite the pending hold will mask

    // A hold tick is in progress, parked in the resolve with the lock held.
    ctx.parkHold.store(true, std::memory_order_release);
    std::thread worker([&] {
      tlsHoldThread = true;
      EXPECT_EQ(dt.hold(), 0u);
    });
    ASSERT_TRUE(ctx.waitParked());

    // The withdrawal arrives while the hold is in progress and must wait for it.
    std::atomic<bool> returned{false};
    std::uint8_t status = 0xFF;
    std::thread command([&] {
      std::vector<std::uint8_t> response;
      std::array<std::uint8_t, 1> index = {0};
      status = (OP == DataTransformOpcode::CLEAR_ALL)
                   ? dt.handleCommand(static_cast<std::uint16_t>(OP), {}, response)
                   : dt.handleCommand(static_cast<std::uint16_t>(OP), {index.data(), index.size()},
                                      response);
      returned.store(true, std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(returned.load(std::memory_order_acquire)) << "opcode " << static_cast<int>(OP);

    ctx.release();
    worker.join();
    command.join();
    EXPECT_EQ(status, 0u) << "opcode " << static_cast<int>(OP);
    EXPECT_EQ(ctx.block[0], 0x00) << "the in-progress hold completed its write";
    EXPECT_EQ(dt.holdApplies(), 1u);

    // After the withdrawal returned, no hold writes the entry.
    ctx.block[0] = 0x34;
    tlsHoldThread = true;
    EXPECT_EQ(dt.hold(), 0u);
    tlsHoldThread = false;
    EXPECT_EQ(ctx.block[0], 0x34) << "opcode " << static_cast<int>(OP);
    EXPECT_EQ(dt.holdApplies(), 1u);
  }
}

/** @test Commanded applies racing hold ticks: every tick either wrote under the lock or skipped,
 * and the block ends masked. */
TEST(DataTransformHold, ConcurrentAppliesAndHoldTicksEachOwnTheirWrites) {
  constexpr unsigned TICKS = 20000;
  DataTransform dt;
  GateCtx ctx;
  armApplyZeroGated(dt, ctx);

  std::atomic<bool> go{false};
  std::thread worker([&] {
    tlsHoldThread = true;
    while (!go.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    for (unsigned i = 0; i < TICKS; ++i) {
      EXPECT_EQ(dt.hold(), 0u);
    }
  });
  go.store(true, std::memory_order_release);
  for (unsigned i = 0; i < TICKS; ++i) {
    ASSERT_EQ(applyEntry(dt, 0), 0u);
  }
  worker.join();

  EXPECT_EQ(dt.holdApplies() + dt.holdSkips(), TICKS);
  EXPECT_EQ(dt.stats().masksApplied, TICKS + 1);
  EXPECT_EQ(ctx.block[0], 0x00);
  EXPECT_EQ(ctx.block[3], 0x00);
  EXPECT_EQ(ctx.block[4], 0xAA);
}

/** @test Hold ticks whose resolve fails count in the hold's own counters while GET_STATS reads the
 * statistics block. */
TEST(DataTransformHold, FailingHoldTicksNeverWriteTheStatisticsBlock) {
  constexpr unsigned TICKS = 20000;
  DataTransform dt;
  GateCtx ctx;
  armApplyZeroGated(dt, ctx);
  ctx.failHold.store(true, std::memory_order_release);

  std::atomic<bool> go{false};
  std::thread worker([&] {
    tlsHoldThread = true;
    while (!go.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    for (unsigned i = 0; i < TICKS; ++i) {
      EXPECT_EQ(dt.hold(), 0u);
    }
  });
  go.store(true, std::memory_order_release);
  std::vector<std::uint8_t> response;
  for (unsigned i = 0; i < TICKS; ++i) {
    ASSERT_EQ(
        dt.handleCommand(static_cast<std::uint16_t>(DataTransformOpcode::GET_STATS), {}, response),
        0u);
  }
  worker.join();

  EXPECT_EQ(dt.holdResolveFailures() + dt.holdSkips(), TICKS);
  EXPECT_EQ(dt.holdApplies(), 0u);
  EXPECT_EQ(dt.stats().resolveFailures, 0u); // the hold's failures are not commanded ones
  EXPECT_EQ(ctx.block[0], 0x00);             // the commanded apply's write stands
}
