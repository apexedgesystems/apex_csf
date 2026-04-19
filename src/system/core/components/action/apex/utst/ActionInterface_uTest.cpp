/**
 * @file ActionInterface_uTest.cpp
 * @brief Unit tests for ActionInterface orchestrator.
 */

#include "src/system/core/components/action/apex/inc/ActionInterface.hpp"

#include <gtest/gtest.h>

#include <cstring>

using system_core::data::ActionInterface;
using system_core::data::ActionStatus;
using system_core::data::ActionTrigger;
using system_core::data::ActionType;
using system_core::data::applyArmControl;
using system_core::data::ArmControlTarget;
using system_core::data::CommandDelegate;
using system_core::data::DataAction;
using system_core::data::DataCategory;
using system_core::data::DataResolveDelegate;
using system_core::data::DataTarget;
using system_core::data::EngineStats;
using system_core::data::initArmControlAction;
using system_core::data::initCommandAction;
using system_core::data::processActions;
using system_core::data::processCycle;
using system_core::data::queueAction;
using system_core::data::removeAction;
using system_core::data::resetInterface;
using system_core::data::ResolvedData;
using system_core::data::resolveTarget;
using system_core::data::shouldActivate;
using system_core::data::WatchDataType;
using system_core::data::WatchPredicate;

/* ----------------------------- Test Helpers ----------------------------- */

/// Simple data block for testing.
struct TestBlock {
  std::array<std::uint8_t, 64> bytes{};
};

/// Context for the resolver delegate.
struct ResolverCtx {
  TestBlock* block{nullptr};
  std::size_t blockSize{0};
  std::uint32_t expectedUid{0};
};

/// Resolver function that returns a fixed block.
ResolvedData testResolver(void* ctx, std::uint32_t fullUid, DataCategory /*cat*/) noexcept {
  auto* c = static_cast<ResolverCtx*>(ctx);
  if (fullUid == c->expectedUid && c->block != nullptr) {
    return {c->block->bytes.data(), c->blockSize};
  }
  return {};
}

/// Context for command delegate.
struct CommandCtx {
  std::uint32_t lastUid{0};
  std::uint16_t lastOpcode{0};
  std::uint8_t lastPayloadLen{0};
  std::uint8_t lastPayload[16]{};
  std::uint32_t callCount{0};
};

/// Command handler function.
void testCommandHandler(void* ctx, std::uint32_t uid, std::uint16_t opcode,
                        const std::uint8_t* payload, std::uint8_t len) noexcept {
  auto* c = static_cast<CommandCtx*>(ctx);
  c->lastUid = uid;
  c->lastOpcode = opcode;
  c->lastPayloadLen = len;
  if (payload != nullptr && len > 0) {
    std::memcpy(c->lastPayload, payload, len);
  }
  ++c->callCount;
}

/// Helper: set up an ActionInterface with resolver and command handler.
ActionInterface makeTestInterface(ResolverCtx& rctx, CommandCtx& cctx) {
  ActionInterface iface{};
  iface.resolver = {testResolver, &rctx};
  iface.commandHandler = {testCommandHandler, &cctx};
  return iface;
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default-constructed ActionInterface has zeroed tables. */
TEST(ActionInterface, DefaultConstruction) {
  const ActionInterface IFACE{};
  EXPECT_EQ(IFACE.actionCount, 0U);
  EXPECT_EQ(IFACE.stats.totalCycles, 0U);
  EXPECT_FALSE(IFACE.resolver);
  EXPECT_FALSE(IFACE.commandHandler);
}

/** @test EngineStats default values. */
TEST(ActionInterface, EngineStatsDefaults) {
  const EngineStats S{};
  EXPECT_EQ(S.totalCycles, 0U);
  EXPECT_EQ(S.watchpointsFired, 0U);
  EXPECT_EQ(S.groupsFired, 0U);
  EXPECT_EQ(S.actionsApplied, 0U);
  EXPECT_EQ(S.commandsRouted, 0U);
  EXPECT_EQ(S.armControlsApplied, 0U);
  EXPECT_EQ(S.sequenceSteps, 0U);
  EXPECT_EQ(S.notificationsInvoked, 0U);
  EXPECT_EQ(S.resolveFailures, 0U);
}

/* ----------------------------- Action Queue ----------------------------- */

/** @test queueAction adds actions and returns ERROR_FULL at capacity. */
TEST(ActionInterface, QueueActionBasic) {
  ActionInterface iface{};

  for (std::uint8_t i = 0; i < system_core::data::ACTION_QUEUE_SIZE; ++i) {
    DataAction action{};
    action.triggerParam = i;
    EXPECT_EQ(queueAction(iface, action), ActionStatus::SUCCESS);
  }
  EXPECT_EQ(iface.actionCount, static_cast<std::uint8_t>(system_core::data::ACTION_QUEUE_SIZE));

  DataAction overflow{};
  EXPECT_EQ(queueAction(iface, overflow), ActionStatus::ERROR_FULL);
}

/** @test removeAction swaps with last and decrements count. */
TEST(ActionInterface, RemoveActionSwap) {
  ActionInterface iface{};

  DataAction a0{};
  a0.triggerParam = 100;
  DataAction a1{};
  a1.triggerParam = 200;
  DataAction a2{};
  a2.triggerParam = 300;

  (void)queueAction(iface, a0);
  (void)queueAction(iface, a1);
  (void)queueAction(iface, a2);
  EXPECT_EQ(iface.actionCount, 3U);

  // Remove index 0: last (300) moves to index 0
  removeAction(iface, 0);
  EXPECT_EQ(iface.actionCount, 2U);
  EXPECT_EQ(iface.actions[0].triggerParam, 300U);
  EXPECT_EQ(iface.actions[1].triggerParam, 200U);
}

/** @test removeAction on last element just decrements count. */
TEST(ActionInterface, RemoveLastAction) {
  ActionInterface iface{};
  DataAction a{};
  a.triggerParam = 42;
  (void)queueAction(iface, a);

  removeAction(iface, 0);
  EXPECT_EQ(iface.actionCount, 0U);
}

/** @test removeAction with out-of-bounds index is no-op. */
TEST(ActionInterface, RemoveOutOfBounds) {
  ActionInterface iface{};
  DataAction a{};
  (void)queueAction(iface, a);

  removeAction(iface, 5);
  EXPECT_EQ(iface.actionCount, 1U);
}

/* ----------------------------- Target Resolution ----------------------------- */

/** @test resolveTarget returns data pointer with correct offset. */
TEST(ActionInterface, ResolveTargetBasic) {
  TestBlock block{};
  block.bytes[4] = 0xAB;
  ResolverCtx rctx{&block, 64, 0x007800};
  CommandCtx cctx{};
  ActionInterface iface = makeTestInterface(rctx, cctx);

  const DataTarget TARGET{0x007800, DataCategory::OUTPUT, 4, 1};
  std::size_t len = 0;
  std::uint8_t* ptr = resolveTarget(iface, TARGET, len);
  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(len, 1U);
  EXPECT_EQ(*ptr, 0xAB);
}

/** @test resolveTarget returns nullptr for unresolvable UID. */
TEST(ActionInterface, ResolveTargetUnknownUid) {
  TestBlock block{};
  ResolverCtx rctx{&block, 64, 0x007800};
  CommandCtx cctx{};
  ActionInterface iface = makeTestInterface(rctx, cctx);

  const DataTarget TARGET{0x999999, DataCategory::OUTPUT, 0, 4};
  std::size_t len = 0;
  EXPECT_EQ(resolveTarget(iface, TARGET, len), nullptr);
}

/** @test resolveTarget returns nullptr when no resolver is set. */
TEST(ActionInterface, ResolveTargetNoResolver) {
  ActionInterface iface{};
  const DataTarget TARGET{0x007800, DataCategory::OUTPUT, 0, 4};
  std::size_t len = 0;
  EXPECT_EQ(resolveTarget(iface, TARGET, len), nullptr);
}

/** @test resolveTarget returns nullptr for out-of-bounds target. */
TEST(ActionInterface, ResolveTargetOutOfBounds) {
  TestBlock block{};
  ResolverCtx rctx{&block, 8, 0x007800}; // Only 8 bytes
  CommandCtx cctx{};
  ActionInterface iface = makeTestInterface(rctx, cctx);

  const DataTarget TARGET{0x007800, DataCategory::OUTPUT, 4, 8}; // 4+8 > 8
  std::size_t len = 0;
  EXPECT_EQ(resolveTarget(iface, TARGET, len), nullptr);
}

/* ----------------------------- shouldActivate ----------------------------- */

/** @test IMMEDIATE trigger activates pending actions. */
TEST(ActionInterface, ShouldActivateImmediate) {
  DataAction a{};
  a.status = ActionStatus::PENDING;
  a.trigger = ActionTrigger::IMMEDIATE;
  EXPECT_TRUE(shouldActivate(a, 0));
}

/** @test AT_CYCLE trigger activates at correct cycle. */
TEST(ActionInterface, ShouldActivateAtCycle) {
  DataAction a{};
  a.status = ActionStatus::PENDING;
  a.trigger = ActionTrigger::AT_CYCLE;
  a.triggerParam = 100;

  EXPECT_FALSE(shouldActivate(a, 50));
  EXPECT_TRUE(shouldActivate(a, 100));
  EXPECT_TRUE(shouldActivate(a, 200));
}

/** @test AFTER_CYCLES trigger activates when elapsed cycles exceed param. */
TEST(ActionInterface, ShouldActivateAfterCycles) {
  DataAction a{};
  a.status = ActionStatus::PENDING;
  a.trigger = ActionTrigger::AFTER_CYCLES;
  a.triggerParam = 50;

  EXPECT_FALSE(shouldActivate(a, 20));
  EXPECT_TRUE(shouldActivate(a, 50));
  EXPECT_TRUE(shouldActivate(a, 100));
}

/** @test AT_TIME trigger never activates via shouldActivate (handled externally). */
TEST(ActionInterface, ShouldActivateAtTime) {
  DataAction a{};
  a.status = ActionStatus::PENDING;
  a.trigger = ActionTrigger::AT_TIME;
  a.triggerParam = 1000;
  EXPECT_FALSE(shouldActivate(a, 1000));
  EXPECT_FALSE(shouldActivate(a, 5000));
}

/** @test ON_EVENT trigger does not activate via shouldActivate. */
TEST(ActionInterface, ShouldActivateOnEvent) {
  DataAction a{};
  a.status = ActionStatus::PENDING;
  a.trigger = ActionTrigger::ON_EVENT;
  a.triggerParam = 5;
  EXPECT_FALSE(shouldActivate(a, 0));
}

/** @test Non-pending actions never activate. */
TEST(ActionInterface, ShouldActivateNotPending) {
  DataAction a{};
  a.status = ActionStatus::ACTIVE;
  a.trigger = ActionTrigger::IMMEDIATE;
  EXPECT_FALSE(shouldActivate(a, 0));
}

/* ----------------------------- COMMAND Processing ----------------------------- */

/** @test COMMAND action routes to commandHandler delegate. */
TEST(ActionInterface, CommandRouting) {
  TestBlock block{};
  ResolverCtx rctx{&block, 64, 0x007800};
  CommandCtx cctx{};
  ActionInterface iface = makeTestInterface(rctx, cctx);

  DataAction action{};
  const std::uint8_t PAYLOAD[] = {0xAA, 0xBB};
  initCommandAction(action, 0x007800, ActionTrigger::IMMEDIATE, 0, 0x42, PAYLOAD, 2);

  (void)queueAction(iface, action);
  processActions(iface, 0);

  EXPECT_EQ(cctx.callCount, 1U);
  EXPECT_EQ(cctx.lastUid, 0x007800U);
  EXPECT_EQ(cctx.lastOpcode, 0x42);
  EXPECT_EQ(cctx.lastPayloadLen, 2U);
  EXPECT_EQ(cctx.lastPayload[0], 0xAA);
  EXPECT_EQ(cctx.lastPayload[1], 0xBB);
  EXPECT_EQ(iface.stats.commandsRouted, 1U);
  EXPECT_EQ(iface.actionCount, 0U); // Command is one-shot
}

/* ----------------------------- ARM_CONTROL Processing ----------------------------- */

/** @test ARM_CONTROL arms a watchpoint by table index. */
TEST(ActionInterface, ArmControlWatchpoint) {
  ActionInterface iface{};
  EXPECT_FALSE(iface.watchpoints[2].armed);

  DataAction action{};
  initArmControlAction(action, ArmControlTarget::WATCHPOINT, 2, true, ActionTrigger::IMMEDIATE, 0);

  (void)queueAction(iface, action);
  processActions(iface, 0);

  EXPECT_TRUE(iface.watchpoints[2].armed);
  EXPECT_EQ(iface.stats.armControlsApplied, 1U);
  EXPECT_EQ(iface.actionCount, 0U);
}

/** @test ARM_CONTROL disarms a sequence and resets it. */
TEST(ActionInterface, ArmControlDisarmSequence) {
  ActionInterface iface{};
  iface.sequences[1].armed = true;
  iface.sequences[1].status = system_core::data::SequenceStatus::WAITING;
  iface.sequences[1].currentStep = 3;

  DataAction action{};
  initArmControlAction(action, ArmControlTarget::SEQUENCE, 1, false, ActionTrigger::IMMEDIATE, 0);

  (void)queueAction(iface, action);
  processActions(iface, 0);

  EXPECT_FALSE(iface.sequences[1].armed);
  EXPECT_EQ(iface.sequences[1].status, system_core::data::SequenceStatus::IDLE);
  EXPECT_EQ(iface.sequences[1].currentStep, 0U);
}

/** @test ARM_CONTROL arms a watchpoint group by table index. */
TEST(ActionInterface, ArmControlGroup) {
  ActionInterface iface{};
  EXPECT_FALSE(iface.groups[1].armed);

  DataAction action{};
  initArmControlAction(action, ArmControlTarget::GROUP, 1, true, ActionTrigger::IMMEDIATE, 0);

  (void)queueAction(iface, action);
  processActions(iface, 0);

  EXPECT_TRUE(iface.groups[1].armed);
  EXPECT_EQ(iface.stats.armControlsApplied, 1U);
  EXPECT_EQ(iface.actionCount, 0U);
}

/** @test ARM_CONTROL with out-of-bounds index is safe. */
TEST(ActionInterface, ArmControlOutOfBounds) {
  ActionInterface iface{};

  DataAction action{};
  initArmControlAction(action, ArmControlTarget::WATCHPOINT, 99, true, ActionTrigger::IMMEDIATE, 0);

  (void)queueAction(iface, action);
  processActions(iface, 0);

  // Should not crash; action still gets cleaned up
  EXPECT_EQ(iface.stats.armControlsApplied, 1U);
  EXPECT_EQ(iface.actionCount, 0U);
}

/* ----------------------------- Watchpoint -> Event Flow ----------------------------- */

/** @test Watchpoint edge fires event, triggers notification callback. */
TEST(ActionInterface, WatchpointFiresNotification) {
  TestBlock block{};
  ResolverCtx rctx{&block, 64, 0x007800};
  CommandCtx cctx{};
  ActionInterface iface = makeTestInterface(rctx, cctx);

  // Watchpoint: byte[0] as UINT8 > 100, eventId=5
  auto& wp = iface.watchpoints[0];
  wp.target = {0x007800, DataCategory::OUTPUT, 0, 1};
  wp.predicate = WatchPredicate::GT;
  wp.dataType = WatchDataType::UINT8;
  wp.eventId = 5;
  wp.threshold[0] = 100;
  wp.armed = true;

  // Notification on eventId=5
  std::uint32_t noteCounter = 0;
  auto noteFn = [](void* ctx, std::uint16_t, std::uint32_t) noexcept {
    ++(*static_cast<std::uint32_t*>(ctx));
  };
  iface.notifications[0].eventId = 5;
  iface.notifications[0].callback = {noteFn, &noteCounter};
  iface.notifications[0].armed = true;

  // Cycle 0: below threshold
  block.bytes[0] = 50;
  system_core::data::rebuildEventIndex(iface);
  processCycle(iface, 0);
  EXPECT_EQ(noteCounter, 0U);
  EXPECT_EQ(iface.stats.watchpointsFired, 0U);

  // Cycle 1: above threshold (edge fires)
  block.bytes[0] = 150;
  processCycle(iface, 1);
  EXPECT_EQ(noteCounter, 1U);
  EXPECT_EQ(iface.stats.watchpointsFired, 1U);
  EXPECT_EQ(iface.stats.notificationsInvoked, 1U);

  // Cycle 2: still above (no edge, no fire)
  processCycle(iface, 2);
  EXPECT_EQ(noteCounter, 1U);
  EXPECT_EQ(iface.stats.watchpointsFired, 1U);
}

/* ----------------------------- Watchpoint Cadence ----------------------------- */

/** @test Watchpoint with cadenceTicks only evaluates on matching ticks. */
TEST(ActionInterface, WatchpointCadence) {
  TestBlock block{};
  ResolverCtx rctx{&block, 64, 0x007800};
  CommandCtx cmdCtx;
  ActionInterface iface{};
  iface.resolver = {testResolver, &rctx};
  iface.commandHandler = {testCommandHandler, &cmdCtx};

  // Watchpoint: byte[0] as UINT8 > 100, eventId=1, cadence=5 (every 5th tick)
  auto& wp = iface.watchpoints[0];
  wp.target = {0x007800, DataCategory::OUTPUT, 0, 1};
  wp.predicate = WatchPredicate::GT;
  wp.dataType = WatchDataType::UINT8;
  wp.threshold[0] = 100;
  wp.eventId = 1;
  wp.armed = true;
  wp.cadenceTicks = 5;

  // Set value above threshold
  block.bytes[0] = 200;

  // Ticks 1-4: watchpoint should NOT evaluate (cadence=5, not divisible)
  for (std::uint32_t t = 1; t <= 4; ++t) {
    processCycle(iface, t);
  }
  EXPECT_EQ(iface.stats.watchpointsFired, 0U) << "Should not fire on non-cadence ticks";

  // Tick 5: watchpoint SHOULD evaluate (5 % 5 == 0)
  processCycle(iface, 5);
  EXPECT_EQ(iface.stats.watchpointsFired, 1U) << "Should fire on cadence tick";

  // Ticks 6-9: should NOT fire again (edge already triggered, value still above)
  for (std::uint32_t t = 6; t <= 9; ++t) {
    processCycle(iface, t);
  }
  EXPECT_EQ(iface.stats.watchpointsFired, 1U) << "Edge should not re-fire";

  // Reset edge by dropping below threshold
  block.bytes[0] = 50;
  processCycle(iface, 10);                     // Cadence tick, evaluates, sees below threshold
  EXPECT_EQ(iface.stats.watchpointsFired, 1U); // No new fire (went low)

  // Go above again
  block.bytes[0] = 200;
  processCycle(iface, 15); // Cadence tick, evaluates, sees edge transition
  EXPECT_EQ(iface.stats.watchpointsFired, 2U) << "Should fire on new edge at cadence tick";
}

/** @test Watchpoint with cadenceTicks=0 evaluates every tick (default). */
TEST(ActionInterface, WatchpointCadenceZeroIsEveryTick) {
  TestBlock block{};
  ResolverCtx rctx{&block, 64, 0x007800};
  CommandCtx cmdCtx;
  ActionInterface iface{};
  iface.resolver = {testResolver, &rctx};
  iface.commandHandler = {testCommandHandler, &cmdCtx};

  auto& wp = iface.watchpoints[0];
  wp.target = {0x007800, DataCategory::OUTPUT, 0, 1};
  wp.predicate = WatchPredicate::GT;
  wp.dataType = WatchDataType::UINT8;
  wp.threshold[0] = 100;
  wp.eventId = 1;
  wp.armed = true;
  wp.cadenceTicks = 0; // Every tick

  block.bytes[0] = 200;
  processCycle(iface, 1);
  EXPECT_EQ(iface.stats.watchpointsFired, 1U) << "cadenceTicks=0 should fire on tick 1";
}

/* ----------------------------- Watchpoint -> Sequence Flow ----------------------------- */

/** @test Watchpoint fires event that starts a sequence. */
TEST(ActionInterface, WatchpointStartsSequence) {
  TestBlock block{};
  ResolverCtx rctx{&block, 64, 0x007800};
  CommandCtx cctx{};
  ActionInterface iface = makeTestInterface(rctx, cctx);

  // Watchpoint: byte[0] UINT8 > 0, eventId=1
  auto& wp = iface.watchpoints[0];
  wp.target = {0x007800, DataCategory::OUTPUT, 0, 1};
  wp.predicate = WatchPredicate::GT;
  wp.dataType = WatchDataType::UINT8;
  wp.eventId = 1;
  wp.threshold[0] = 0;
  wp.armed = true;

  // Sequence: on eventId=1, step 0 sends a command with no delay
  auto& seq = iface.sequences[0];
  initCommandAction(seq.steps[0].action, 0x007800, ActionTrigger::IMMEDIATE, 0, 0x0010);
  seq.steps[0].delayCycles = 0;
  seq.stepCount = 1;
  seq.eventId = 1;
  seq.armed = true;

  // Cycle 0: below threshold, no fire
  block.bytes[0] = 0;
  system_core::data::rebuildEventIndex(iface);
  processCycle(iface, 0);
  EXPECT_EQ(cctx.callCount, 0U);

  // Cycle 1: above threshold, edge fires, sequence starts
  block.bytes[0] = 1;
  processCycle(iface, 1);

  // Sequence step action was queued and processed in the same cycle
  EXPECT_EQ(iface.stats.sequenceSteps, 1U);
  EXPECT_EQ(iface.stats.commandsRouted, 1U);

  // Command was routed to the handler
  EXPECT_EQ(cctx.callCount, 1U);
  EXPECT_EQ(cctx.lastUid, 0x007800U);
  EXPECT_EQ(cctx.lastOpcode, 0x0010U);
}

/* ----------------------------- ON_EVENT Action ----------------------------- */

/** @test ON_EVENT action activates when matching event fires. */
TEST(ActionInterface, OnEventAction) {
  TestBlock block{};
  ResolverCtx rctx{&block, 64, 0x007800};
  CommandCtx cctx{};
  ActionInterface iface = makeTestInterface(rctx, cctx);

  // Watchpoint: byte[8] UINT8 > 0, eventId=3
  auto& wp = iface.watchpoints[0];
  wp.target = {0x007800, DataCategory::OUTPUT, 8, 1};
  wp.predicate = WatchPredicate::GT;
  wp.dataType = WatchDataType::UINT8;
  wp.eventId = 3;
  wp.threshold[0] = 0;
  wp.armed = true;

  // ON_EVENT action: send command when eventId=3 fires
  DataAction action{};
  initCommandAction(action, 0x007800, ActionTrigger::ON_EVENT, 3, 0x0020);
  (void)queueAction(iface, action);

  // Cycle 0: watchpoint not triggered
  block.bytes[8] = 0;
  processCycle(iface, 0);
  EXPECT_EQ(cctx.callCount, 0U);

  // Cycle 1: watchpoint fires, ON_EVENT action activates and routes command
  block.bytes[8] = 1;
  processCycle(iface, 1);
  EXPECT_EQ(cctx.callCount, 1U);
  EXPECT_EQ(cctx.lastOpcode, 0x0020U);
}

/* ----------------------------- Group -> Event ----------------------------- */

/** @test Watchpoint group combines two watchpoints with AND logic. */
TEST(ActionInterface, GroupAndLogic) {
  TestBlock block{};
  ResolverCtx rctx{&block, 64, 0x007800};
  CommandCtx cctx{};
  ActionInterface iface = makeTestInterface(rctx, cctx);

  // Watchpoint 0: byte[0] > 100
  auto& wp0 = iface.watchpoints[0];
  wp0.watchpointId = 1;
  wp0.target = {0x007800, DataCategory::OUTPUT, 0, 1};
  wp0.predicate = WatchPredicate::GT;
  wp0.dataType = WatchDataType::UINT8;
  wp0.threshold[0] = 100;
  wp0.armed = true;

  // Watchpoint 1: byte[1] > 100
  auto& wp1 = iface.watchpoints[1];
  wp1.watchpointId = 2;
  wp1.target = {0x007800, DataCategory::OUTPUT, 1, 1};
  wp1.predicate = WatchPredicate::GT;
  wp1.dataType = WatchDataType::UINT8;
  wp1.threshold[0] = 100;
  wp1.armed = true;

  // Group: AND of wp0 and wp1, eventId=10
  auto& group = iface.groups[0];
  group.refs[0] = 1;
  group.refs[1] = 2;
  group.count = 2;
  group.logic = system_core::data::GroupLogic::AND;
  group.eventId = 10;
  group.armed = true;

  // Rebuild watchpoint ID-to-index lookup for group evaluation
  system_core::data::rebuildWatchpointIndex(iface);

  // Notification on eventId=10
  std::uint32_t noteCounter = 0;
  auto noteFn = [](void* ctx, std::uint16_t, std::uint32_t) noexcept {
    ++(*static_cast<std::uint32_t*>(ctx));
  };
  iface.notifications[0].eventId = 10;
  iface.notifications[0].callback = {noteFn, &noteCounter};
  iface.notifications[0].armed = true;

  // Rebuild event index AFTER notifications are configured
  system_core::data::rebuildEventIndex(iface);

  // Cycle 0: only wp0 true -> group false
  block.bytes[0] = 150;
  block.bytes[1] = 50;
  processCycle(iface, 0);
  EXPECT_EQ(noteCounter, 0U);

  // Cycle 1: both true -> group fires
  block.bytes[1] = 150;
  processCycle(iface, 1);
  EXPECT_EQ(noteCounter, 1U);
  EXPECT_EQ(iface.stats.groupsFired, 1U);
}

/* ----------------------------- Layered Fault Detection ----------------------------- */

/** @test Layered scenario: warning arms critical watchpoint via ARM_CONTROL. */
TEST(ActionInterface, LayeredFaultDetection) {
  TestBlock block{};
  ResolverCtx rctx{&block, 64, 0x007800};
  CommandCtx cctx{};
  ActionInterface iface = makeTestInterface(rctx, cctx);

  // Warning watchpoint (index 0): byte[0] > 100, eventId=1
  auto& warnWp = iface.watchpoints[0];
  warnWp.target = {0x007800, DataCategory::OUTPUT, 0, 1};
  warnWp.predicate = WatchPredicate::GT;
  warnWp.dataType = WatchDataType::UINT8;
  warnWp.eventId = 1;
  warnWp.threshold[0] = 100;
  warnWp.armed = true;

  // Critical watchpoint (index 1): byte[0] > 200, eventId=2
  // Starts DISARMED
  auto& critWp = iface.watchpoints[1];
  critWp.target = {0x007800, DataCategory::OUTPUT, 0, 1};
  critWp.predicate = WatchPredicate::GT;
  critWp.dataType = WatchDataType::UINT8;
  critWp.eventId = 2;
  critWp.threshold[0] = 200;
  critWp.armed = false;

  // ON_EVENT action: when eventId=1 fires, arm watchpoint index 1
  DataAction armAction{};
  initArmControlAction(armAction, ArmControlTarget::WATCHPOINT, 1, true, ActionTrigger::ON_EVENT,
                       1);
  (void)queueAction(iface, armAction);

  // Notification on eventId=2 (critical)
  std::uint32_t critCounter = 0;
  auto noteFn = [](void* ctx, std::uint16_t, std::uint32_t) noexcept {
    ++(*static_cast<std::uint32_t*>(ctx));
  };
  iface.notifications[0].eventId = 2;
  iface.notifications[0].callback = {noteFn, &critCounter};
  iface.notifications[0].armed = true;

  // Cycle 0: value below warning threshold
  block.bytes[0] = 50;
  system_core::data::rebuildEventIndex(iface);
  processCycle(iface, 0);
  EXPECT_FALSE(iface.watchpoints[1].armed);

  // Cycle 1: value above warning threshold, edge fires eventId=1
  //          ARM_CONTROL action arms critical watchpoint
  block.bytes[0] = 150;
  processCycle(iface, 1);
  EXPECT_TRUE(iface.watchpoints[1].armed);

  // Cycle 2: value above critical threshold, critical wp fires eventId=2
  block.bytes[0] = 250;
  processCycle(iface, 2);
  EXPECT_EQ(critCounter, 1U);
}

/* ----------------------------- Sequence with Delay ----------------------------- */

/** @test Sequence with delay waits before executing step action. */
TEST(ActionInterface, SequenceWithDelay) {
  TestBlock block{};
  ResolverCtx rctx{&block, 64, 0x007800};
  CommandCtx cctx{};
  ActionInterface iface = makeTestInterface(rctx, cctx);

  // Sequence: step 0 has 2-cycle delay, then sends command
  auto& seq = iface.sequences[0];
  initCommandAction(seq.steps[0].action, 0x007800, ActionTrigger::IMMEDIATE, 0, 0x0030);
  seq.steps[0].delayCycles = 2;
  seq.stepCount = 1;
  seq.eventId = 1;
  seq.armed = true;

  // Watchpoint fires eventId=1 immediately
  auto& wp = iface.watchpoints[0];
  wp.target = {0x007800, DataCategory::OUTPUT, 4, 1};
  wp.predicate = WatchPredicate::GT;
  wp.dataType = WatchDataType::UINT8;
  wp.eventId = 1;
  wp.threshold[0] = 0;
  wp.armed = true;
  block.bytes[4] = 1;

  // Cycle 0: watchpoint fires, sequence starts (delay=2), first tick (2->1)
  system_core::data::rebuildEventIndex(iface);
  processCycle(iface, 0);
  EXPECT_EQ(cctx.callCount, 0U); // Not yet applied

  // Cycle 1: second tick (1->0), delay reaches zero, step executes
  processCycle(iface, 1);
  EXPECT_EQ(cctx.callCount, 1U);
  EXPECT_EQ(cctx.lastOpcode, 0x0030U);
  EXPECT_EQ(iface.stats.sequenceSteps, 1U);
}

/* ----------------------------- processCycle Stats ----------------------------- */

/** @test processCycle increments totalCycles. */
TEST(ActionInterface, ProcessCycleIncrementsCycles) {
  ActionInterface iface{};
  processCycle(iface, 0);
  processCycle(iface, 1);
  processCycle(iface, 2);
  EXPECT_EQ(iface.stats.totalCycles, 3U);
}

/* ----------------------------- resetInterface ----------------------------- */

/** @test resetInterface clears tables but preserves delegates. */
TEST(ActionInterface, ResetPreservesDelegates) {
  TestBlock block{};
  ResolverCtx rctx{&block, 64, 0x007800};
  CommandCtx cctx{};
  ActionInterface iface = makeTestInterface(rctx, cctx);

  // Put some state in the tables
  iface.watchpoints[0].armed = true;
  iface.watchpoints[0].eventId = 5;
  iface.groups[0].armed = true;
  iface.sequences[0].armed = true;
  DataAction action{};
  (void)queueAction(iface, action);
  iface.stats.totalCycles = 100;

  resetInterface(iface);

  // Tables should be cleared
  EXPECT_FALSE(iface.watchpoints[0].armed);
  EXPECT_EQ(iface.watchpoints[0].eventId, 0U);
  EXPECT_FALSE(iface.groups[0].armed);
  EXPECT_FALSE(iface.sequences[0].armed);
  EXPECT_EQ(iface.actionCount, 0U);
  EXPECT_EQ(iface.stats.totalCycles, 0U);

  // Delegates should be preserved
  EXPECT_TRUE(static_cast<bool>(iface.resolver));
  EXPECT_TRUE(static_cast<bool>(iface.commandHandler));
}

/* ----------------------------- Sequence Timeout Tests ----------------------------- */

/** @test Step timeout with ABORT policy sets TIMED_OUT and increments stats. */
TEST(ActionInterface, SequenceTimeoutAbort) {
  TestBlock block{};
  ResolverCtx rctx{&block, 64, 0x007800};
  CommandCtx cctx{};
  ActionInterface iface = makeTestInterface(rctx, cctx);

  auto& seq = iface.sequences[0];
  seq.stepCount = 1;
  seq.steps[0].delayCycles = 100; // Long delay
  seq.steps[0].timeoutCycles = 3; // Times out in 3 cycles
  seq.steps[0].onTimeout = system_core::data::StepTimeoutPolicy::ABORT;
  seq.armed = true;

  system_core::data::startSequence(seq);
  EXPECT_EQ(seq.steps[0].timeoutRemaining, 3U);

  // Tick 3 times: timeout fires on tick 3
  processCycle(iface, 0); // timeout: 3->2
  EXPECT_TRUE(system_core::data::isRunning(seq));
  processCycle(iface, 1); // timeout: 2->1
  EXPECT_TRUE(system_core::data::isRunning(seq));
  processCycle(iface, 2); // timeout: 1->0, fires ABORT

  EXPECT_EQ(seq.status, system_core::data::SequenceStatus::TIMED_OUT);
  EXPECT_EQ(iface.stats.sequenceTimeouts, 1U);
  EXPECT_EQ(iface.stats.sequenceAborts, 1U);
}

/** @test Step timeout with SKIP policy advances to next step. */
TEST(ActionInterface, SequenceTimeoutSkipToNext) {
  TestBlock block{};
  ResolverCtx rctx{&block, 64, 0x007800};
  CommandCtx cctx{};
  ActionInterface iface = makeTestInterface(rctx, cctx);

  auto& seq = iface.sequences[0];
  seq.stepCount = 2;
  seq.steps[0].delayCycles = 100;
  seq.steps[0].timeoutCycles = 2;
  seq.steps[0].onTimeout = system_core::data::StepTimeoutPolicy::SKIP;
  initCommandAction(seq.steps[1].action, 0x007800, ActionTrigger::IMMEDIATE, 0, 0x0040);
  seq.steps[1].delayCycles = 0;
  seq.armed = true;

  system_core::data::startSequence(seq);

  // Tick 2 times: timeout fires, skips to step 1
  processCycle(iface, 0);
  processCycle(iface, 1);
  EXPECT_EQ(iface.stats.sequenceTimeouts, 1U);

  // Step 1 should execute (zero delay, no timeout)
  processCycle(iface, 2);
  EXPECT_EQ(cctx.callCount, 1U);
  EXPECT_EQ(cctx.lastOpcode, 0x0040U);
  EXPECT_EQ(iface.stats.sequenceSteps, 1U);
}

/** @test Step timeout with retry exhaustion before skip. */
TEST(ActionInterface, SequenceTimeoutRetryThenSkip) {
  TestBlock block{};
  ResolverCtx rctx{&block, 64, 0x007800};
  CommandCtx cctx{};
  ActionInterface iface = makeTestInterface(rctx, cctx);

  auto& seq = iface.sequences[0];
  seq.stepCount = 2;
  seq.steps[0].delayCycles = 100;
  seq.steps[0].timeoutCycles = 1; // Times out after 1 cycle
  seq.steps[0].onTimeout = system_core::data::StepTimeoutPolicy::SKIP;
  seq.steps[0].retryMax = 1; // 1 retry
  seq.steps[1].delayCycles = 0;
  seq.armed = true;

  system_core::data::startSequence(seq);

  // Tick 1: timeout -> retry (step stays at 0)
  processCycle(iface, 0);
  EXPECT_EQ(seq.currentStep, 0U);
  EXPECT_EQ(seq.steps[0].retryCount, 1U);
  EXPECT_EQ(iface.stats.sequenceRetries, 1U);

  // Tick 2: timeout again -> retries exhausted, skip to step 1
  processCycle(iface, 1);
  EXPECT_EQ(seq.currentStep, 1U);
  EXPECT_EQ(iface.stats.sequenceTimeouts, 2U);
}

/* ----------------------------- Sequence Command Dispatch ----------------------------- */

/** @test COMMAND action in sequence dispatches via commandHandler. */
TEST(ActionInterface, SequenceCommandDispatch) {
  TestBlock block{};
  ResolverCtx rctx{&block, 64, 0x007800};
  CommandCtx cctx{};
  ActionInterface iface = makeTestInterface(rctx, cctx);

  auto& seq = iface.sequences[0];
  initCommandAction(seq.steps[0].action, 0x007800, ActionTrigger::IMMEDIATE, 0, 0x0000);
  seq.steps[0].delayCycles = 0;
  seq.stepCount = 1;
  seq.armed = true;

  system_core::data::startSequence(seq);

  // Process: sequence dispatches command
  processCycle(iface, 0);

  EXPECT_EQ(cctx.callCount, 1U);
  EXPECT_EQ(cctx.lastUid, 0x007800U);
  EXPECT_EQ(cctx.lastOpcode, 0x0000U);
  EXPECT_EQ(iface.stats.sequenceSteps, 1U);
  EXPECT_EQ(iface.stats.commandsRouted, 1U);
}

/* ----------------------------- Sequence Wait Condition ----------------------------- */

/** @test Wait condition holds step until data matches. */
TEST(ActionInterface, SequenceWaitCondition) {
  TestBlock block{};
  block.bytes[0] = 0xFF;
  ResolverCtx rctx{&block, 64, 0x007800};
  CommandCtx cctx{};
  ActionInterface iface = makeTestInterface(rctx, cctx);

  auto& seq = iface.sequences[0];
  initCommandAction(seq.steps[0].action, 0x007800, ActionTrigger::IMMEDIATE, 0, 0x0050);
  seq.steps[0].delayCycles = 1; // 1-cycle delay first
  seq.steps[0].waitCondition.enabled = true;
  seq.steps[0].waitCondition.target = {0x007800, DataCategory::OUTPUT, 0, 4};
  seq.steps[0].waitCondition.predicate = WatchPredicate::EQ;
  seq.steps[0].waitCondition.dataType = WatchDataType::UINT32;
  std::uint32_t threshold = 42;
  std::memcpy(seq.steps[0].waitCondition.threshold.data(), &threshold, 4);
  seq.stepCount = 1;
  seq.armed = true;

  system_core::data::startSequence(seq);

  // Cycle 0: delay countdown (1->0), transitions to WAITING_CONDITION
  processCycle(iface, 0);
  EXPECT_EQ(seq.status, system_core::data::SequenceStatus::WAITING_CONDITION);

  // Cycle 1: condition not met (block[0..3] = 0xFF000000, not 42)
  processCycle(iface, 1);
  EXPECT_EQ(seq.status, system_core::data::SequenceStatus::WAITING_CONDITION);

  // Set the value to match
  std::uint32_t val = 42;
  std::memcpy(block.bytes.data(), &val, 4);

  // Cycle 2: condition met, step executes
  processCycle(iface, 2);
  EXPECT_EQ(iface.stats.sequenceSteps, 1U);
}

/* ----------------------------- START_RTS Cross-Slot ----------------------------- */

/** @test START_RTS completion action triggers another sequence slot. */
TEST(ActionInterface, SequenceStartRts) {
  TestBlock block{};
  ResolverCtx rctx{&block, 64, 0x007800};
  CommandCtx cctx{};
  ActionInterface iface = makeTestInterface(rctx, cctx);

  // Sequence 0: one step that triggers sequence 1 on completion (by ID)
  auto& seq0 = iface.sequences[0];
  seq0.sequenceId = 100;
  initCommandAction(seq0.steps[0].action, 0x007800, ActionTrigger::IMMEDIATE, 0, 0x0001);
  seq0.steps[0].delayCycles = 0;
  seq0.steps[0].onComplete = system_core::data::StepCompletionAction::START_RTS;
  seq0.steps[0].chainTargetId = 200; // Chain to sequence ID 200
  seq0.stepCount = 1;
  seq0.armed = true;

  // Sequence 1 (ID=200): one step that sends a second command
  auto& seq1 = iface.sequences[1];
  seq1.sequenceId = 200;
  initCommandAction(seq1.steps[0].action, 0x007800, ActionTrigger::IMMEDIATE, 0, 0x0002);
  seq1.steps[0].delayCycles = 0;
  seq1.stepCount = 1;
  seq1.armed = true;

  system_core::data::startSequence(seq0);

  // Cycle 0: seq0 step fires (command 0x0001), START_RTS triggers seq1.
  // seq1 also ticks in the same cycle (slot 1 processed after slot 0).
  processCycle(iface, 0);
  EXPECT_EQ(cctx.callCount, 2U);
  EXPECT_EQ(cctx.lastOpcode, 0x0002U); // seq1 command was last
  EXPECT_EQ(iface.stats.sequenceSteps, 2U);
  EXPECT_EQ(iface.stats.commandsRouted, 2U);
}

/* ----------------------------- ATS Absolute Timing ----------------------------- */

/** @test ATS step fires at absolute cycle offset from start. */
TEST(ActionInterface, AtsAbsoluteTiming) {
  TestBlock block{};
  ResolverCtx rctx{&block, 64, 0x007800};
  CommandCtx cctx{};
  ActionInterface iface = makeTestInterface(rctx, cctx);

  auto& seq = iface.sequences[0];
  initCommandAction(seq.steps[0].action, 0x007800, ActionTrigger::IMMEDIATE, 0, 0x0060);
  seq.steps[0].delayCycles = 5; // Fire at startCycle + 5
  seq.type = system_core::data::SequenceType::ATS;
  seq.stepCount = 1;
  seq.armed = true;

  // Start at cycle 10
  system_core::data::startSequence(seq, 10);
  EXPECT_EQ(seq.startTime, 10U);

  // Cycles 10-14: not yet
  for (std::uint32_t c = 10; c < 15; ++c) {
    processCycle(iface, c);
    EXPECT_EQ(cctx.callCount, 0U) << "Should not fire at cycle " << c;
  }

  // Cycle 15: fires (startCycle=10 + delayCycles=5 = 15)
  processCycle(iface, 15);
  EXPECT_EQ(cctx.callCount, 1U);
  EXPECT_EQ(cctx.lastOpcode, 0x0060U);
  EXPECT_EQ(iface.stats.sequenceSteps, 1U);
}

/* ----------------------------- Event Sequence Handler ----------------------------- */

/** @test eventSequenceHandler is called when no resident sequence matches event. */
TEST(ActionInterface, EventSequenceHandlerCalledOnNoMatch) {
  TestBlock block{};
  ResolverCtx rctx{&block, 64, 0x007800};
  CommandCtx cctx;
  ActionInterface iface{};
  iface.resolver = {testResolver, &rctx};
  iface.commandHandler = {testCommandHandler, &cctx};

  // Watchpoint: byte[0] > 100, eventId=42
  auto& wp = iface.watchpoints[0];
  wp.target = {0x007800, DataCategory::OUTPUT, 0, 1};
  wp.predicate = WatchPredicate::GT;
  wp.dataType = WatchDataType::UINT8;
  wp.threshold[0] = 100;
  wp.eventId = 42;
  wp.armed = true;

  // No resident sequence with eventId=42 — handler should be called
  std::uint16_t handlerEventId = 0;
  std::uint32_t handlerCallCount = 0;
  auto handlerFn = [](void* ctx, std::uint16_t eventId) {
    auto* counter = static_cast<std::uint32_t*>(ctx);
    ++(*counter);
    // Store eventId in the word after the counter
    *(reinterpret_cast<std::uint16_t*>(counter + 1)) = eventId;
  };

  struct HandlerCtx {
    std::uint32_t callCount{0};
    std::uint16_t lastEventId{0};
    static void handler(void* ctx, std::uint16_t eventId) noexcept {
      auto* h = static_cast<HandlerCtx*>(ctx);
      ++h->callCount;
      h->lastEventId = eventId;
    }
  } hctx;

  iface.eventSequenceHandler = {HandlerCtx::handler, &hctx};

  // Trigger watchpoint
  block.bytes[0] = 200;
  processCycle(iface, 1);

  EXPECT_EQ(hctx.callCount, 1U) << "Handler should be called when no resident sequence matches";
  EXPECT_EQ(hctx.lastEventId, 42U) << "Handler should receive the correct eventId";
}

/** @test eventSequenceHandler NOT called when a resident sequence matches. */
TEST(ActionInterface, EventSequenceHandlerNotCalledWhenResidentMatches) {
  TestBlock block{};
  ResolverCtx rctx{&block, 64, 0x007800};
  CommandCtx cctx;
  ActionInterface iface{};
  iface.resolver = {testResolver, &rctx};
  iface.commandHandler = {testCommandHandler, &cctx};

  // Watchpoint: byte[0] > 100, eventId=42
  auto& wp = iface.watchpoints[0];
  wp.target = {0x007800, DataCategory::OUTPUT, 0, 1};
  wp.predicate = WatchPredicate::GT;
  wp.dataType = WatchDataType::UINT8;
  wp.threshold[0] = 100;
  wp.eventId = 42;
  wp.armed = true;

  // Resident sequence with eventId=42 — handler should NOT be called
  auto& seq = iface.sequences[0];
  seq.eventId = 42;
  seq.armed = true;
  seq.stepCount = 1;
  seq.steps[0].delayCycles = 0xFFFFFFFF; // Long delay

  struct HandlerCtx2 {
    std::uint32_t callCount{0};
    static void handler(void* ctx, std::uint16_t /*eventId*/) noexcept {
      auto* h = static_cast<HandlerCtx2*>(ctx);
      ++h->callCount;
    }
  } hctx;

  iface.eventSequenceHandler = {HandlerCtx2::handler, &hctx};

  block.bytes[0] = 200;
  system_core::data::rebuildEventIndex(iface);
  processCycle(iface, 1);

  EXPECT_EQ(hctx.callCount, 0U) << "Handler should NOT be called when resident sequence matches";
}

/* ----------------------------- Abort Event Dispatch ----------------------------- */

/** @test Abort event dispatches through notification pipeline when sequence is aborted. */
TEST(ActionInterface, AbortEventDispatchesNotification) {
  TestBlock block{};
  ResolverCtx rctx{&block, 64, 0x007800};
  CommandCtx cctx;
  ActionInterface iface{};
  iface.resolver = {testResolver, &rctx};
  iface.commandHandler = {testCommandHandler, &cctx};

  // Sequence with abort event
  auto& seq = iface.sequences[0];
  seq.stepCount = 1;
  seq.steps[0].delayCycles = 0;
  seq.armed = true;
  seq.abortEventId = 200;
  system_core::data::startSequence(seq);
  EXPECT_TRUE(system_core::data::isRunning(seq));

  // Notification listening for abort event (needs callback or log message)
  struct NoteCtx {
    std::uint32_t callCount{0};
    static void handler(void* ctx, std::uint16_t, std::uint32_t) noexcept {
      ++static_cast<NoteCtx*>(ctx)->callCount;
    }
  } noteCtx;
  auto& note = iface.notifications[0];
  note.eventId = 200;
  note.armed = true;
  note.callback = {NoteCtx::handler, &noteCtx};

  // Abort the sequence
  system_core::data::abortSequence(seq);
  EXPECT_TRUE(seq.abortEventPending);

  // processCycle dispatches abort events
  system_core::data::rebuildEventIndex(iface);
  processCycle(iface, 1);

  EXPECT_FALSE(seq.abortEventPending);
  EXPECT_EQ(noteCtx.callCount, 1U);
  EXPECT_EQ(note.invokeCount, 1U);
  EXPECT_EQ(iface.stats.abortEventsDispatched, 1U);
}

/** @test Abort event triggers cleanup sequence when dispatched. */
TEST(ActionInterface, AbortEventTriggersCleanupSequence) {
  TestBlock block{};
  ResolverCtx rctx{&block, 64, 0x007800};
  CommandCtx cctx;
  ActionInterface iface{};
  iface.resolver = {testResolver, &rctx};
  iface.commandHandler = {testCommandHandler, &cctx};

  // Sequence A: will be aborted, fires event 300 on abort
  auto& seqA = iface.sequences[0];
  seqA.stepCount = 1;
  seqA.steps[0].delayCycles = 0;
  seqA.armed = true;
  seqA.abortEventId = 300;
  system_core::data::startSequence(seqA);

  // Sequence B: cleanup sequence, triggered by event 300
  auto& seqB = iface.sequences[1];
  seqB.stepCount = 1;
  seqB.steps[0].delayCycles = 0;
  seqB.steps[0].action.actionType = ActionType::COMMAND;
  seqB.steps[0].action.target = {0x007800, DataCategory::OUTPUT, 0, 1};
  seqB.steps[0].action.commandOpcode = 0x9999;
  seqB.eventId = 300;
  seqB.armed = true;

  // Abort sequence A
  system_core::data::abortSequence(seqA);

  // processCycle dispatches abort event -> starts cleanup sequence
  system_core::data::rebuildEventIndex(iface);
  processCycle(iface, 1);

  EXPECT_TRUE(system_core::data::isRunning(seqB))
      << "Cleanup sequence should be started by abort event";
  EXPECT_EQ(seqB.runCount, 1U);
}

/** @test No abort event dispatch when abortEventId is 0. */
TEST(ActionInterface, NoAbortEventWhenIdIsZero) {
  ActionInterface iface{};

  auto& seq = iface.sequences[0];
  seq.stepCount = 1;
  seq.armed = true;
  seq.abortEventId = 0;
  system_core::data::startSequence(seq);
  system_core::data::abortSequence(seq);

  processCycle(iface, 1);
  EXPECT_EQ(iface.stats.abortEventsDispatched, 0U);
}

/** @test Timeout-caused abort also fires abort event. */
TEST(ActionInterface, TimeoutAbortFiresAbortEvent) {
  TestBlock block{};
  ResolverCtx rctx{&block, 64, 0x007800};
  ActionInterface iface{};
  iface.resolver = {testResolver, &rctx};

  // Sequence with wait condition that never fires (forces timeout).
  // delayCycles=1 so step waits one tick, then transitions to WAITING_CONDITION.
  auto& seq = iface.sequences[0];
  seq.stepCount = 1;
  seq.steps[0].delayCycles = 1;
  seq.steps[0].timeoutCycles = 3;
  seq.steps[0].onTimeout = system_core::data::StepTimeoutPolicy::ABORT;
  seq.steps[0].waitCondition.enabled = true;
  seq.steps[0].waitCondition.target = {0x007800, DataCategory::OUTPUT, 0, 1};
  seq.steps[0].waitCondition.predicate = WatchPredicate::EQ;
  seq.steps[0].waitCondition.dataType = WatchDataType::UINT8;
  seq.steps[0].waitCondition.threshold[0] = 0xFF; // Never matches (block is zeroed)
  seq.armed = true;
  seq.abortEventId = 400;

  // Notification for abort event (needs callback)
  struct NoteCtx {
    std::uint32_t callCount{0};
    static void handler(void* ctx, std::uint16_t, std::uint32_t) noexcept {
      ++static_cast<NoteCtx*>(ctx)->callCount;
    }
  } noteCtx;
  auto& note = iface.notifications[0];
  note.eventId = 400;
  note.armed = true;
  note.callback = {NoteCtx::handler, &noteCtx};

  system_core::data::startSequence(seq);
  system_core::data::rebuildEventIndex(iface);

  // Tick 1: delay countdown (1 -> 0), wait condition engages
  processCycle(iface, 1);
  EXPECT_EQ(seq.status, system_core::data::SequenceStatus::WAITING_CONDITION);

  // Tick 2: condition not met, timeout counting (2 remaining)
  processCycle(iface, 2);
  EXPECT_EQ(seq.status, system_core::data::SequenceStatus::WAITING_CONDITION);

  // Tick 3: condition not met, timeout counting (1 remaining)
  processCycle(iface, 3);

  // Tick 4: timeout expires -> TIMED_OUT, abort event dispatched
  processCycle(iface, 4);

  EXPECT_EQ(seq.status, system_core::data::SequenceStatus::TIMED_OUT);
  EXPECT_GE(noteCtx.callCount, 1U);
  EXPECT_GE(iface.stats.abortEventsDispatched, 1U);
}
