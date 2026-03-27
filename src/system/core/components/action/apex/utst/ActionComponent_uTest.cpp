/**
 * @file ActionComponent_uTest.cpp
 * @brief Unit tests for ActionComponent lifecycle and delegation.
 */

#include "src/system/core/components/action/apex/inc/ActionComponent.hpp"
#include "src/system/core/components/action/apex/inc/ActionComponentStatus.hpp"

#include <cstdint>
#include <cstring>

#include <array>

#include <gtest/gtest.h>

/* ----------------------------- Test Helpers ----------------------------- */

struct TestBlock {
  std::array<std::uint8_t, 64> bytes{};
};

struct ResolverCtx {
  TestBlock* block{nullptr};
  std::uint32_t lastUid{0};
};

static system_core::data::ResolvedData
testResolver(void* ctx, std::uint32_t uid, system_core::data::DataCategory /*cat*/) noexcept {
  auto* r = static_cast<ResolverCtx*>(ctx);
  r->lastUid = uid;
  if (r->block == nullptr) {
    return {};
  }
  return {r->block->bytes.data(), r->block->bytes.size()};
}

struct CommandCtx {
  std::uint32_t lastUid{0};
  std::uint16_t lastOpcode{0};
  std::uint8_t callCount{0};
};

static void testCmdHandler(void* ctx, std::uint32_t uid, std::uint16_t opcode,
                           const std::uint8_t* /*payload*/, std::uint8_t /*len*/) noexcept {
  auto* c = static_cast<CommandCtx*>(ctx);
  c->lastUid = uid;
  c->lastOpcode = opcode;
  ++c->callCount;
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Component identity returns correct ID and name. */
TEST(ActionComponent, ComponentIdentity) {
  system_core::action::ActionComponent comp;
  EXPECT_EQ(comp.componentId(), 5U);
  EXPECT_STREQ(comp.componentName(), "Action");
  EXPECT_STREQ(comp.label(), "ACTION");
}

/** @test Default construction yields configured but uninitialized state. */
TEST(ActionComponent, DefaultState) {
  system_core::action::ActionComponent comp;
  EXPECT_FALSE(comp.isInitialized());
  EXPECT_TRUE(comp.isConfigured());
  EXPECT_EQ(comp.stats().totalCycles, 0U);
}

/* ----------------------------- Init Tests ----------------------------- */

/** @test Init fails without resolver delegate. */
TEST(ActionComponent, InitFailsWithoutResolver) {
  system_core::action::ActionComponent comp;
  const std::uint8_t RESULT = comp.init();
  EXPECT_EQ(RESULT, static_cast<std::uint8_t>(system_core::action::Status::ERROR_NO_RESOLVER));
  EXPECT_FALSE(comp.isInitialized());
}

/** @test Init succeeds with resolver set. */
TEST(ActionComponent, InitSucceeds) {
  system_core::action::ActionComponent comp;
  ResolverCtx rCtx;
  comp.setResolver(testResolver, &rCtx);
  const std::uint8_t RESULT = comp.init();
  EXPECT_EQ(RESULT, static_cast<std::uint8_t>(system_core::action::Status::SUCCESS));
  EXPECT_TRUE(comp.isInitialized());
}

/** @test Init is idempotent. */
TEST(ActionComponent, InitIdempotent) {
  system_core::action::ActionComponent comp;
  ResolverCtx rCtx;
  comp.setResolver(testResolver, &rCtx);
  EXPECT_EQ(comp.init(), static_cast<std::uint8_t>(system_core::action::Status::SUCCESS));
  EXPECT_TRUE(comp.isInitialized());
  const std::uint8_t RESULT = comp.init();
  EXPECT_EQ(RESULT, static_cast<std::uint8_t>(system_core::action::Status::SUCCESS));
}

/* ----------------------------- Reset Tests ----------------------------- */

/** @test Reset clears tables and stats but preserves delegates. */
TEST(ActionComponent, ResetPreservesDelegates) {
  system_core::action::ActionComponent comp;
  ResolverCtx rCtx;
  TestBlock block;
  rCtx.block = &block;
  CommandCtx cCtx;
  comp.setResolver(testResolver, &rCtx);
  comp.setCommandHandler(testCmdHandler, &cCtx);
  EXPECT_EQ(comp.init(), static_cast<std::uint8_t>(system_core::action::Status::SUCCESS));

  // Run a cycle to bump stats
  comp.tick(0);
  EXPECT_EQ(comp.stats().totalCycles, 1U);

  // Reset
  comp.reset();
  EXPECT_FALSE(comp.isInitialized());
  EXPECT_EQ(comp.stats().totalCycles, 0U);

  // Resolver still works after reset (configured flag preserved by reset())
  const std::uint8_t RESULT = comp.init();
  EXPECT_EQ(RESULT, static_cast<std::uint8_t>(system_core::action::Status::SUCCESS));
}

/* ----------------------------- Tick Tests ----------------------------- */

/** @test Tick increments totalCycles. */
TEST(ActionComponent, TickIncrementsCycles) {
  system_core::action::ActionComponent comp;
  ResolverCtx rCtx;
  TestBlock block;
  rCtx.block = &block;
  comp.setResolver(testResolver, &rCtx);
  EXPECT_EQ(comp.init(), static_cast<std::uint8_t>(system_core::action::Status::SUCCESS));

  comp.tick(0);
  comp.tick(1);
  comp.tick(2);
  EXPECT_EQ(comp.stats().totalCycles, 3U);
}

/** @test Tick with armed watchpoint triggers event flow. */
TEST(ActionComponent, TickWithWatchpoint) {
  system_core::action::ActionComponent comp;
  TestBlock block;
  ResolverCtx rCtx{&block};
  CommandCtx cCtx;
  comp.setResolver(testResolver, &rCtx);
  comp.setCommandHandler(testCmdHandler, &cCtx);
  EXPECT_EQ(comp.init(), static_cast<std::uint8_t>(system_core::action::Status::SUCCESS));

  // Set up a watchpoint: byte[0] > 0
  auto& wp = comp.iface().watchpoints[0];
  wp.target = {0x007800, system_core::data::DataCategory::OUTPUT, 0, 1};
  wp.predicate = system_core::data::WatchPredicate::GT;
  wp.dataType = system_core::data::WatchDataType::UINT8;
  wp.eventId = 42;
  wp.threshold[0] = 0;
  wp.armed = true;

  // Set data to trigger: byte[0] = 5
  block.bytes[0] = 5;

  comp.tick(0);
  EXPECT_EQ(comp.stats().watchpointsFired, 1U);
}

/* ----------------------------- Interface Access ----------------------------- */

/** @test Mutable iface() allows direct table configuration. */
TEST(ActionComponent, IfaceAccess) {
  system_core::action::ActionComponent comp;

  auto& iface = comp.iface();
  iface.watchpoints[0].armed = true;
  iface.watchpoints[0].eventId = 99;

  const auto& constIface = std::as_const(comp).iface();
  EXPECT_TRUE(constIface.watchpoints[0].armed);
  EXPECT_EQ(constIface.watchpoints[0].eventId, 99U);
}

/* ----------------------------- Status Tests ----------------------------- */

/** @test Status toString covers all values. */
TEST(ActionComponentStatus, ToStringCoversAll) {
  using system_core::action::Status;
  using system_core::action::toString;

  EXPECT_STREQ(toString(Status::SUCCESS), "SUCCESS");
  EXPECT_STREQ(toString(Status::ERROR_NO_RESOLVER), "ERROR_NO_RESOLVER");
  EXPECT_STREQ(toString(Status::ERROR_QUEUE_FULL), "ERROR_QUEUE_FULL");
  EXPECT_STREQ(toString(Status::WARN_RESOLVE_FAILURES), "WARN_RESOLVE_FAILURES");
  EXPECT_STREQ(toString(Status::EOE_ACTION), "EOE_ACTION");
}
