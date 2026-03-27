/**
 * @file DataSequence_uTest.cpp
 * @brief Unit tests for DataSequence ordered action lists.
 */

#include "src/system/core/infrastructure/data/inc/DataSequence.hpp"

#include <gtest/gtest.h>

#include <cstring>

using system_core::data::ActionTrigger;
using system_core::data::DataAction;
using system_core::data::DataCategory;
using system_core::data::DataSequence;
using system_core::data::DataTarget;
using system_core::data::SEQUENCE_MAX_STEPS;
using system_core::data::SEQUENCE_REPEAT_FOREVER;
using system_core::data::SEQUENCE_TABLE_SIZE;
using system_core::data::SequenceStatus;
using system_core::data::SequenceStep;
using system_core::data::SequenceType;
using system_core::data::StepCompletionAction;
using system_core::data::StepTimeoutPolicy;
using system_core::data::StepWaitCondition;
using system_core::data::WatchDataType;
using system_core::data::WatchPredicate;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default DataSequence is idle, unarmed, zero steps. */
TEST(DataSequence, DefaultConstruction) {
  DataSequence seq{};

  EXPECT_EQ(seq.stepCount, 0U);
  EXPECT_EQ(seq.currentStep, 0U);
  EXPECT_EQ(seq.type, SequenceType::RTS);
  EXPECT_EQ(seq.status, SequenceStatus::IDLE);
  EXPECT_EQ(seq.eventId, 0U);
  EXPECT_EQ(seq.delayRemaining, 0U);
  EXPECT_FALSE(seq.armed);
  EXPECT_EQ(seq.runCount, 0U);
  EXPECT_EQ(seq.sequenceId, 0U);
  EXPECT_EQ(seq.startTime, 0U);
}

/** @test Default SequenceStep has zero delay and default action. */
TEST(DataSequence, DefaultStepConstruction) {
  SequenceStep step{};

  EXPECT_EQ(step.delayCycles, 0U);
  EXPECT_EQ(step.action.trigger, ActionTrigger{});
  EXPECT_EQ(step.timeoutCycles, 0U);
  EXPECT_EQ(step.onTimeout, StepTimeoutPolicy::ABORT);
  EXPECT_EQ(step.onComplete, StepCompletionAction::NEXT);
  EXPECT_EQ(step.gotoStep, 0U);
  EXPECT_EQ(step.retryMax, 0U);
  EXPECT_EQ(step.retryCount, 0U);
  EXPECT_EQ(step.timeoutRemaining, 0U);
  EXPECT_FALSE(step.waitCondition.enabled);
}

/** @test Default StepWaitCondition is disabled with sane defaults. */
TEST(DataSequence, DefaultWaitConditionConstruction) {
  StepWaitCondition cond{};

  EXPECT_FALSE(cond.enabled);
  EXPECT_EQ(cond.predicate, WatchPredicate::EQ);
  EXPECT_EQ(cond.dataType, WatchDataType::RAW);
  EXPECT_EQ(cond.target.fullUid, 0U);
}

/* ----------------------------- Enum Tests ----------------------------- */

/** @test SequenceType toString round-trip. */
TEST(DataSequence, SequenceTypeToString) {
  EXPECT_STREQ(system_core::data::toString(SequenceType::RTS), "RTS");
  EXPECT_STREQ(system_core::data::toString(SequenceType::ATS), "ATS");
}

/** @test SequenceStatus toString round-trip. */
TEST(DataSequence, SequenceStatusToString) {
  EXPECT_STREQ(system_core::data::toString(SequenceStatus::IDLE), "IDLE");
  EXPECT_STREQ(system_core::data::toString(SequenceStatus::WAITING), "WAITING");
  EXPECT_STREQ(system_core::data::toString(SequenceStatus::EXECUTING), "EXECUTING");
  EXPECT_STREQ(system_core::data::toString(SequenceStatus::COMPLETE), "COMPLETE");
  EXPECT_STREQ(system_core::data::toString(SequenceStatus::WAITING_CONDITION), "WAITING_CONDITION");
  EXPECT_STREQ(system_core::data::toString(SequenceStatus::TIMED_OUT), "TIMED_OUT");
  EXPECT_STREQ(system_core::data::toString(SequenceStatus::ABORTED), "ABORTED");
}

/** @test StepTimeoutPolicy toString round-trip. */
TEST(DataSequence, StepTimeoutPolicyToString) {
  EXPECT_STREQ(system_core::data::toString(StepTimeoutPolicy::ABORT), "ABORT");
  EXPECT_STREQ(system_core::data::toString(StepTimeoutPolicy::SKIP), "SKIP");
  EXPECT_STREQ(system_core::data::toString(StepTimeoutPolicy::GOTO_STEP), "GOTO_STEP");
}

/** @test StepCompletionAction toString round-trip. */
TEST(DataSequence, StepCompletionActionToString) {
  EXPECT_STREQ(system_core::data::toString(StepCompletionAction::NEXT), "NEXT");
  EXPECT_STREQ(system_core::data::toString(StepCompletionAction::GOTO_STEP), "GOTO_STEP");
  EXPECT_STREQ(system_core::data::toString(StepCompletionAction::START_RTS), "START_RTS");
}

/* ----------------------------- Constants ----------------------------- */

/** @test Table sizes are reasonable. */
TEST(DataSequence, ConstantSanity) {
  EXPECT_GE(SEQUENCE_MAX_STEPS, 4U);
  EXPECT_GE(SEQUENCE_TABLE_SIZE, 2U);
  EXPECT_EQ(SEQUENCE_MAX_STEPS, 16U);
  EXPECT_EQ(SEQUENCE_TABLE_SIZE, 8U);
}

/* ----------------------------- startSequence ----------------------------- */

/** @test Starting a sequence with zero delay goes straight to EXECUTING. */
TEST(DataSequence, StartZeroDelay) {
  DataSequence seq{};
  seq.stepCount = 1;
  seq.steps[0].delayCycles = 0;
  seq.armed = true;

  system_core::data::startSequence(seq);

  EXPECT_EQ(seq.status, SequenceStatus::EXECUTING);
  EXPECT_EQ(seq.currentStep, 0U);
  EXPECT_EQ(seq.delayRemaining, 0U);
  EXPECT_EQ(seq.runCount, 1U);
}

/** @test Starting a sequence with nonzero delay goes to WAITING. */
TEST(DataSequence, StartWithDelay) {
  DataSequence seq{};
  seq.stepCount = 2;
  seq.steps[0].delayCycles = 10;
  seq.armed = true;

  system_core::data::startSequence(seq);

  EXPECT_EQ(seq.status, SequenceStatus::WAITING);
  EXPECT_EQ(seq.delayRemaining, 10U);
  EXPECT_EQ(seq.runCount, 1U);
}

/** @test Starting an unarmed sequence does nothing. */
TEST(DataSequence, StartUnarmed) {
  DataSequence seq{};
  seq.stepCount = 1;
  seq.armed = false;

  system_core::data::startSequence(seq);

  EXPECT_EQ(seq.status, SequenceStatus::IDLE);
  EXPECT_EQ(seq.runCount, 0U);
}

/** @test Starting a sequence with zero steps does nothing. */
TEST(DataSequence, StartZeroSteps) {
  DataSequence seq{};
  seq.stepCount = 0;
  seq.armed = true;

  system_core::data::startSequence(seq);

  EXPECT_EQ(seq.status, SequenceStatus::IDLE);
  EXPECT_EQ(seq.runCount, 0U);
}

/** @test Restarting a running sequence resets to step 0. */
TEST(DataSequence, PreemptiveRestart) {
  DataSequence seq{};
  seq.stepCount = 3;
  seq.steps[0].delayCycles = 5;
  seq.steps[1].delayCycles = 5;
  seq.steps[2].delayCycles = 5;
  seq.armed = true;

  system_core::data::startSequence(seq);
  EXPECT_EQ(seq.runCount, 1U);

  // Simulate partial progress
  seq.currentStep = 2;
  seq.status = SequenceStatus::WAITING;

  // Restart
  system_core::data::startSequence(seq);
  EXPECT_EQ(seq.currentStep, 0U);
  EXPECT_EQ(seq.delayRemaining, 5U);
  EXPECT_EQ(seq.runCount, 2U);
}

/** @test startSequence captures start reference as startTime. */
TEST(DataSequence, StartCapturesStartTime) {
  DataSequence seq{};
  seq.stepCount = 1;
  seq.steps[0].delayCycles = 0;
  seq.armed = true;

  system_core::data::startSequence(seq, 500);

  EXPECT_EQ(seq.startTime, 500U);
}

/** @test startSequence initializes timeout on first step. */
TEST(DataSequence, StartInitializesTimeout) {
  DataSequence seq{};
  seq.stepCount = 1;
  seq.steps[0].delayCycles = 10;
  seq.steps[0].timeoutCycles = 200;
  seq.armed = true;

  system_core::data::startSequence(seq);

  EXPECT_EQ(seq.steps[0].timeoutRemaining, 200U);
}

/* ----------------------------- tickSequence ----------------------------- */

/** @test Ticking an IDLE sequence returns nullptr. */
TEST(DataSequence, TickIdleReturnsNull) {
  DataSequence seq{};
  seq.status = SequenceStatus::IDLE;

  bool ready = false;
  const DataAction* action = system_core::data::tickSequence(seq, ready);

  EXPECT_EQ(action, nullptr);
  EXPECT_FALSE(ready);
}

/** @test Ticking a COMPLETE sequence returns nullptr. */
TEST(DataSequence, TickCompleteReturnsNull) {
  DataSequence seq{};
  seq.status = SequenceStatus::COMPLETE;

  bool ready = false;
  const DataAction* action = system_core::data::tickSequence(seq, ready);

  EXPECT_EQ(action, nullptr);
  EXPECT_FALSE(ready);
}

/** @test Ticking a TIMED_OUT sequence returns nullptr. */
TEST(DataSequence, TickTimedOutReturnsNull) {
  DataSequence seq{};
  seq.status = SequenceStatus::TIMED_OUT;

  bool ready = false;
  const DataAction* action = system_core::data::tickSequence(seq, ready);

  EXPECT_EQ(action, nullptr);
  EXPECT_FALSE(ready);
}

/** @test Ticking an ABORTED sequence returns nullptr. */
TEST(DataSequence, TickAbortedReturnsNull) {
  DataSequence seq{};
  seq.status = SequenceStatus::ABORTED;

  bool ready = false;
  const DataAction* action = system_core::data::tickSequence(seq, ready);

  EXPECT_EQ(action, nullptr);
  EXPECT_FALSE(ready);
}

/** @test Delay countdown works correctly. */
TEST(DataSequence, TickCountdown) {
  DataSequence seq{};
  seq.stepCount = 1;
  seq.steps[0].delayCycles = 3;
  seq.armed = true;

  system_core::data::startSequence(seq);
  EXPECT_EQ(seq.status, SequenceStatus::WAITING);

  bool ready = false;

  // Tick 1: 3->2
  EXPECT_EQ(system_core::data::tickSequence(seq, ready), nullptr);
  EXPECT_FALSE(ready);
  EXPECT_EQ(seq.delayRemaining, 2U);

  // Tick 2: 2->1
  EXPECT_EQ(system_core::data::tickSequence(seq, ready), nullptr);
  EXPECT_FALSE(ready);
  EXPECT_EQ(seq.delayRemaining, 1U);

  // Tick 3: 1->0, action ready
  const DataAction* action = system_core::data::tickSequence(seq, ready);
  EXPECT_TRUE(ready);
  EXPECT_NE(action, nullptr);
  EXPECT_EQ(seq.status, SequenceStatus::EXECUTING);
}

/** @test EXECUTING status returns nullptr (waiting for advanceStep). */
TEST(DataSequence, TickExecutingReturnsNull) {
  DataSequence seq{};
  seq.stepCount = 1;
  seq.steps[0].delayCycles = 0;
  seq.armed = true;
  seq.status = SequenceStatus::EXECUTING;
  seq.currentStep = 0;

  bool ready = false;
  EXPECT_EQ(system_core::data::tickSequence(seq, ready), nullptr);
  EXPECT_FALSE(ready);
}

/** @test Delay done with wait condition transitions to WAITING_CONDITION. */
TEST(DataSequence, TickTransitionsToWaitingCondition) {
  DataSequence seq{};
  seq.stepCount = 1;
  seq.steps[0].delayCycles = 1;
  seq.steps[0].waitCondition.enabled = true;
  seq.armed = true;

  system_core::data::startSequence(seq);
  EXPECT_EQ(seq.status, SequenceStatus::WAITING);

  bool ready = false;
  const DataAction* action = system_core::data::tickSequence(seq, ready);
  EXPECT_EQ(action, nullptr);
  EXPECT_FALSE(ready);
  EXPECT_EQ(seq.status, SequenceStatus::WAITING_CONDITION);
}

/* ----------------------------- advanceStep ----------------------------- */

/** @test Advancing past last step completes the sequence. */
TEST(DataSequence, AdvancePastLastStep) {
  DataSequence seq{};
  seq.stepCount = 1;
  seq.currentStep = 0;
  seq.status = SequenceStatus::EXECUTING;

  system_core::data::advanceStep(seq);

  EXPECT_EQ(seq.status, SequenceStatus::COMPLETE);
  EXPECT_EQ(seq.currentStep, 1U);
}

/** @test Advancing to a step with delay enters WAITING. */
TEST(DataSequence, AdvanceToDelayedStep) {
  DataSequence seq{};
  seq.stepCount = 2;
  seq.steps[1].delayCycles = 20;
  seq.currentStep = 0;
  seq.status = SequenceStatus::EXECUTING;

  system_core::data::advanceStep(seq);

  EXPECT_EQ(seq.status, SequenceStatus::WAITING);
  EXPECT_EQ(seq.currentStep, 1U);
  EXPECT_EQ(seq.delayRemaining, 20U);
}

/** @test Advancing to a step with zero delay stays EXECUTING. */
TEST(DataSequence, AdvanceToImmediateStep) {
  DataSequence seq{};
  seq.stepCount = 2;
  seq.steps[1].delayCycles = 0;
  seq.currentStep = 0;
  seq.status = SequenceStatus::EXECUTING;

  system_core::data::advanceStep(seq);

  EXPECT_EQ(seq.status, SequenceStatus::EXECUTING);
  EXPECT_EQ(seq.currentStep, 1U);
  EXPECT_EQ(seq.delayRemaining, 0U);
}

/** @test Advancing when not EXECUTING does nothing. */
TEST(DataSequence, AdvanceWhenNotExecuting) {
  DataSequence seq{};
  seq.stepCount = 2;
  seq.currentStep = 0;
  seq.status = SequenceStatus::WAITING;

  system_core::data::advanceStep(seq);

  EXPECT_EQ(seq.status, SequenceStatus::WAITING);
  EXPECT_EQ(seq.currentStep, 0U);
}

/** @test advanceStep initializes timeout on next step. */
TEST(DataSequence, AdvanceInitializesNextTimeout) {
  DataSequence seq{};
  seq.stepCount = 2;
  seq.steps[1].delayCycles = 5;
  seq.steps[1].timeoutCycles = 100;
  seq.currentStep = 0;
  seq.status = SequenceStatus::EXECUTING;

  system_core::data::advanceStep(seq);

  EXPECT_EQ(seq.steps[1].timeoutRemaining, 100U);
}

/** @test advanceStep resets retryCount on next step. */
TEST(DataSequence, AdvanceResetsRetryCount) {
  DataSequence seq{};
  seq.stepCount = 2;
  seq.steps[1].retryCount = 3; // Leftover from a previous run
  seq.currentStep = 0;
  seq.status = SequenceStatus::EXECUTING;

  system_core::data::advanceStep(seq);

  EXPECT_EQ(seq.steps[1].retryCount, 0U);
}

/* ----------------------------- advanceStep with startSlot ----------------------------- */

/** @test NEXT completion returns startSlot=0xFF. */
TEST(DataSequence, AdvanceNextNoStartSlot) {
  DataSequence seq{};
  seq.stepCount = 2;
  seq.steps[0].onComplete = StepCompletionAction::NEXT;
  seq.currentStep = 0;
  seq.status = SequenceStatus::EXECUTING;

  std::uint8_t startSlot = 0;
  system_core::data::advanceStep(seq, startSlot);

  EXPECT_EQ(startSlot, 0xFF);
  EXPECT_EQ(seq.currentStep, 1U);
}

/** @test GOTO_STEP completion jumps to target step. */
TEST(DataSequence, AdvanceGotoStep) {
  DataSequence seq{};
  seq.stepCount = 4;
  seq.steps[0].onComplete = StepCompletionAction::GOTO_STEP;
  seq.steps[0].gotoStep = 3;
  seq.steps[3].delayCycles = 5;
  seq.currentStep = 0;
  seq.status = SequenceStatus::EXECUTING;

  std::uint8_t startSlot = 0;
  system_core::data::advanceStep(seq, startSlot);

  EXPECT_EQ(startSlot, 0xFF);
  EXPECT_EQ(seq.currentStep, 3U);
  EXPECT_EQ(seq.status, SequenceStatus::WAITING);
  EXPECT_EQ(seq.delayRemaining, 5U);
}

/** @test START_RTS completion advances linearly and sets startSlot. */
TEST(DataSequence, AdvanceStartRts) {
  DataSequence seq{};
  seq.stepCount = 2;
  seq.steps[0].onComplete = StepCompletionAction::START_RTS;
  seq.steps[0].gotoStep = 3; // Slot 3
  seq.currentStep = 0;
  seq.status = SequenceStatus::EXECUTING;

  std::uint8_t startSlot = 0xFF;
  system_core::data::advanceStep(seq, startSlot);

  EXPECT_EQ(startSlot, 3U);
  EXPECT_EQ(seq.currentStep, 1U);
}

/* ----------------------------- Timeout Tests ----------------------------- */

/** @test ABORT timeout policy sets TIMED_OUT status. */
TEST(DataSequence, TimeoutAbort) {
  DataSequence seq{};
  seq.stepCount = 2;
  seq.steps[0].timeoutCycles = 5;
  seq.steps[0].onTimeout = StepTimeoutPolicy::ABORT;
  seq.currentStep = 0;
  seq.status = SequenceStatus::WAITING;

  const bool CONTINUES = system_core::data::applyTimeoutPolicy(seq);

  EXPECT_FALSE(CONTINUES);
  EXPECT_EQ(seq.status, SequenceStatus::TIMED_OUT);
}

/** @test SKIP timeout policy advances to next step. */
TEST(DataSequence, TimeoutSkip) {
  DataSequence seq{};
  seq.stepCount = 3;
  seq.steps[0].timeoutCycles = 5;
  seq.steps[0].onTimeout = StepTimeoutPolicy::SKIP;
  seq.steps[1].delayCycles = 10;
  seq.steps[1].timeoutCycles = 50;
  seq.currentStep = 0;
  seq.status = SequenceStatus::WAITING;

  const bool CONTINUES = system_core::data::applyTimeoutPolicy(seq);

  EXPECT_TRUE(CONTINUES);
  EXPECT_EQ(seq.currentStep, 1U);
  EXPECT_EQ(seq.delayRemaining, 10U);
  EXPECT_EQ(seq.steps[1].timeoutRemaining, 50U);
}

/** @test SKIP timeout on last step completes the sequence. */
TEST(DataSequence, TimeoutSkipLastStep) {
  DataSequence seq{};
  seq.stepCount = 1;
  seq.steps[0].onTimeout = StepTimeoutPolicy::SKIP;
  seq.currentStep = 0;
  seq.status = SequenceStatus::WAITING;

  const bool CONTINUES = system_core::data::applyTimeoutPolicy(seq);

  EXPECT_FALSE(CONTINUES);
  EXPECT_EQ(seq.status, SequenceStatus::COMPLETE);
}

/** @test GOTO_STEP timeout jumps to target step. */
TEST(DataSequence, TimeoutGotoStep) {
  DataSequence seq{};
  seq.stepCount = 4;
  seq.steps[0].timeoutCycles = 5;
  seq.steps[0].onTimeout = StepTimeoutPolicy::GOTO_STEP;
  seq.steps[0].gotoStep = 3;
  seq.steps[3].delayCycles = 0;
  seq.steps[3].timeoutCycles = 20;
  seq.currentStep = 0;
  seq.status = SequenceStatus::WAITING;

  const bool CONTINUES = system_core::data::applyTimeoutPolicy(seq);

  EXPECT_TRUE(CONTINUES);
  EXPECT_EQ(seq.currentStep, 3U);
  EXPECT_EQ(seq.status, SequenceStatus::EXECUTING);
  EXPECT_EQ(seq.steps[3].timeoutRemaining, 20U);
}

/** @test GOTO_STEP with out-of-bounds target aborts. */
TEST(DataSequence, TimeoutGotoStepOutOfBounds) {
  DataSequence seq{};
  seq.stepCount = 2;
  seq.steps[0].onTimeout = StepTimeoutPolicy::GOTO_STEP;
  seq.steps[0].gotoStep = 10; // Out of bounds
  seq.currentStep = 0;
  seq.status = SequenceStatus::WAITING;

  const bool CONTINUES = system_core::data::applyTimeoutPolicy(seq);

  EXPECT_FALSE(CONTINUES);
  EXPECT_EQ(seq.status, SequenceStatus::TIMED_OUT);
}

/* ----------------------------- Retry Tests ----------------------------- */

/** @test SKIP with retry restarts step instead of skipping. */
TEST(DataSequence, RetryOnTimeout) {
  DataSequence seq{};
  seq.stepCount = 2;
  seq.steps[0].delayCycles = 5;
  seq.steps[0].timeoutCycles = 10;
  seq.steps[0].onTimeout = StepTimeoutPolicy::SKIP;
  seq.steps[0].retryMax = 2;
  seq.steps[0].retryCount = 0;
  seq.currentStep = 0;
  seq.status = SequenceStatus::WAITING;

  // First timeout: retry 1
  bool continues = system_core::data::applyTimeoutPolicy(seq);
  EXPECT_TRUE(continues);
  EXPECT_EQ(seq.currentStep, 0U); // Same step
  EXPECT_EQ(seq.steps[0].retryCount, 1U);
  EXPECT_EQ(seq.delayRemaining, 5U);
  EXPECT_EQ(seq.steps[0].timeoutRemaining, 10U);

  // Second timeout: retry 2
  continues = system_core::data::applyTimeoutPolicy(seq);
  EXPECT_TRUE(continues);
  EXPECT_EQ(seq.currentStep, 0U);
  EXPECT_EQ(seq.steps[0].retryCount, 2U);

  // Third timeout: retries exhausted, skip to next step
  continues = system_core::data::applyTimeoutPolicy(seq);
  EXPECT_TRUE(continues);
  EXPECT_EQ(seq.currentStep, 1U);
}

/** @test Retry count is zero when retryMax is zero (no retries). */
TEST(DataSequence, NoRetryWhenMaxZero) {
  DataSequence seq{};
  seq.stepCount = 2;
  seq.steps[0].onTimeout = StepTimeoutPolicy::SKIP;
  seq.steps[0].retryMax = 0;
  seq.steps[0].retryCount = 0;
  seq.currentStep = 0;
  seq.status = SequenceStatus::WAITING;

  const bool CONTINUES = system_core::data::applyTimeoutPolicy(seq);
  EXPECT_TRUE(CONTINUES);
  EXPECT_EQ(seq.currentStep, 1U); // Skipped immediately
}

/* ----------------------------- resetSequence ----------------------------- */

/** @test Reset puts sequence back to IDLE. */
TEST(DataSequence, Reset) {
  DataSequence seq{};
  seq.stepCount = 3;
  seq.currentStep = 2;
  seq.status = SequenceStatus::EXECUTING;
  seq.delayRemaining = 42;
  seq.runCount = 5;

  system_core::data::resetSequence(seq);

  EXPECT_EQ(seq.status, SequenceStatus::IDLE);
  EXPECT_EQ(seq.currentStep, 0U);
  EXPECT_EQ(seq.delayRemaining, 0U);
  // runCount preserved
  EXPECT_EQ(seq.runCount, 5U);
}

/* ----------------------------- abortSequence ----------------------------- */

/** @test Abort a running sequence sets ABORTED status. */
TEST(DataSequence, AbortRunning) {
  DataSequence seq{};
  seq.status = SequenceStatus::WAITING;

  system_core::data::abortSequence(seq);
  EXPECT_EQ(seq.status, SequenceStatus::ABORTED);
}

/** @test Abort a WAITING_CONDITION sequence sets ABORTED. */
TEST(DataSequence, AbortWaitingCondition) {
  DataSequence seq{};
  seq.status = SequenceStatus::WAITING_CONDITION;

  system_core::data::abortSequence(seq);
  EXPECT_EQ(seq.status, SequenceStatus::ABORTED);
}

/** @test Abort an IDLE sequence does nothing. */
TEST(DataSequence, AbortIdleNoOp) {
  DataSequence seq{};
  seq.status = SequenceStatus::IDLE;

  system_core::data::abortSequence(seq);
  EXPECT_EQ(seq.status, SequenceStatus::IDLE);
}

/** @test Abort a COMPLETE sequence does nothing. */
TEST(DataSequence, AbortCompleteNoOp) {
  DataSequence seq{};
  seq.status = SequenceStatus::COMPLETE;

  system_core::data::abortSequence(seq);
  EXPECT_EQ(seq.status, SequenceStatus::COMPLETE);
}

/* ----------------------------- shouldTrigger ----------------------------- */

/** @test shouldTrigger matches armed sequence with correct eventId. */
TEST(DataSequence, ShouldTriggerMatch) {
  DataSequence seq{};
  seq.eventId = 42;
  seq.armed = true;

  EXPECT_TRUE(system_core::data::shouldTrigger(seq, 42));
}

/** @test shouldTrigger rejects wrong eventId. */
TEST(DataSequence, ShouldTriggerWrongEvent) {
  DataSequence seq{};
  seq.eventId = 42;
  seq.armed = true;

  EXPECT_FALSE(system_core::data::shouldTrigger(seq, 99));
}

/** @test shouldTrigger rejects unarmed sequence. */
TEST(DataSequence, ShouldTriggerUnarmed) {
  DataSequence seq{};
  seq.eventId = 42;
  seq.armed = false;

  EXPECT_FALSE(system_core::data::shouldTrigger(seq, 42));
}

/* ----------------------------- Status Queries ----------------------------- */

/** @test isComplete returns true only for COMPLETE status. */
TEST(DataSequence, IsComplete) {
  DataSequence seq{};
  seq.status = SequenceStatus::COMPLETE;
  EXPECT_TRUE(system_core::data::isComplete(seq));

  seq.status = SequenceStatus::IDLE;
  EXPECT_FALSE(system_core::data::isComplete(seq));
}

/** @test isRunning returns true for WAITING, EXECUTING, or WAITING_CONDITION. */
TEST(DataSequence, IsRunning) {
  DataSequence seq{};

  seq.status = SequenceStatus::WAITING;
  EXPECT_TRUE(system_core::data::isRunning(seq));

  seq.status = SequenceStatus::EXECUTING;
  EXPECT_TRUE(system_core::data::isRunning(seq));

  seq.status = SequenceStatus::WAITING_CONDITION;
  EXPECT_TRUE(system_core::data::isRunning(seq));

  seq.status = SequenceStatus::IDLE;
  EXPECT_FALSE(system_core::data::isRunning(seq));

  seq.status = SequenceStatus::COMPLETE;
  EXPECT_FALSE(system_core::data::isRunning(seq));

  seq.status = SequenceStatus::TIMED_OUT;
  EXPECT_FALSE(system_core::data::isRunning(seq));

  seq.status = SequenceStatus::ABORTED;
  EXPECT_FALSE(system_core::data::isRunning(seq));
}

/* ----------------------------- evaluateStepCondition ----------------------------- */

/** @test Disabled condition always returns true. */
TEST(DataSequence, EvalConditionDisabled) {
  StepWaitCondition cond{};
  cond.enabled = false;

  std::uint8_t data[] = {0x00};
  EXPECT_TRUE(system_core::data::evaluateStepCondition(cond, data, 1));
}

/** @test Enabled condition with EQ predicate matches. */
TEST(DataSequence, EvalConditionEqMatch) {
  StepWaitCondition cond{};
  cond.enabled = true;
  cond.predicate = WatchPredicate::EQ;
  cond.dataType = WatchDataType::UINT32;
  cond.target.byteLen = 4;

  std::uint32_t threshold = 42;
  std::memcpy(cond.threshold.data(), &threshold, 4);

  std::uint32_t value = 42;
  EXPECT_TRUE(
      system_core::data::evaluateStepCondition(cond, reinterpret_cast<std::uint8_t*>(&value), 4));
}

/** @test Enabled condition with EQ predicate does not match. */
TEST(DataSequence, EvalConditionEqNoMatch) {
  StepWaitCondition cond{};
  cond.enabled = true;
  cond.predicate = WatchPredicate::EQ;
  cond.dataType = WatchDataType::UINT32;
  cond.target.byteLen = 4;

  std::uint32_t threshold = 42;
  std::memcpy(cond.threshold.data(), &threshold, 4);

  std::uint32_t value = 99;
  EXPECT_FALSE(
      system_core::data::evaluateStepCondition(cond, reinterpret_cast<std::uint8_t*>(&value), 4));
}

/** @test Null data returns true (same as disabled). */
TEST(DataSequence, EvalConditionNullData) {
  StepWaitCondition cond{};
  cond.enabled = true;

  EXPECT_TRUE(system_core::data::evaluateStepCondition(cond, nullptr, 0));
}

/** @test GT predicate with float data. */
TEST(DataSequence, EvalConditionGtFloat) {
  StepWaitCondition cond{};
  cond.enabled = true;
  cond.predicate = WatchPredicate::GT;
  cond.dataType = WatchDataType::FLOAT32;
  cond.target.byteLen = 4;

  float threshold = 100.0F;
  std::memcpy(cond.threshold.data(), &threshold, 4);

  float above = 150.0F;
  EXPECT_TRUE(
      system_core::data::evaluateStepCondition(cond, reinterpret_cast<std::uint8_t*>(&above), 4));

  float below = 50.0F;
  EXPECT_FALSE(
      system_core::data::evaluateStepCondition(cond, reinterpret_cast<std::uint8_t*>(&below), 4));
}

/* ----------------------------- Full Sequence Execution ----------------------------- */

/** @test Run a 3-step sequence to completion. */
TEST(DataSequence, FullThreeStepExecution) {
  DataSequence seq{};
  seq.type = SequenceType::RTS;
  seq.eventId = 10;
  seq.stepCount = 3;
  seq.steps[0].delayCycles = 2;
  seq.steps[1].delayCycles = 1;
  seq.steps[2].delayCycles = 0;
  seq.armed = true;

  system_core::data::startSequence(seq);
  EXPECT_EQ(seq.status, SequenceStatus::WAITING);

  bool ready = false;

  // Step 0: 2 tick delay
  EXPECT_EQ(system_core::data::tickSequence(seq, ready), nullptr); // 2->1
  EXPECT_FALSE(ready);
  const DataAction* a = system_core::data::tickSequence(seq, ready); // 1->0
  EXPECT_TRUE(ready);
  EXPECT_NE(a, nullptr);
  EXPECT_EQ(seq.currentStep, 0U);

  system_core::data::advanceStep(seq);
  EXPECT_EQ(seq.currentStep, 1U);
  EXPECT_EQ(seq.status, SequenceStatus::WAITING);

  // Step 1: 1 tick delay
  a = system_core::data::tickSequence(seq, ready); // 1->0
  EXPECT_TRUE(ready);
  EXPECT_NE(a, nullptr);

  system_core::data::advanceStep(seq);
  EXPECT_EQ(seq.currentStep, 2U);
  EXPECT_EQ(seq.status, SequenceStatus::EXECUTING);

  // Step 2: 0 delay -> already EXECUTING
  system_core::data::advanceStep(seq);
  EXPECT_EQ(seq.status, SequenceStatus::COMPLETE);
  EXPECT_TRUE(system_core::data::isComplete(seq));
  EXPECT_FALSE(system_core::data::isRunning(seq));
}

/** @test Zero-delay sequence executes all steps immediately via advanceStep chain. */
TEST(DataSequence, AllZeroDelay) {
  DataSequence seq{};
  seq.stepCount = 3;
  seq.steps[0].delayCycles = 0;
  seq.steps[1].delayCycles = 0;
  seq.steps[2].delayCycles = 0;
  seq.armed = true;

  system_core::data::startSequence(seq);
  // Step 0 starts EXECUTING immediately
  EXPECT_EQ(seq.status, SequenceStatus::EXECUTING);

  system_core::data::advanceStep(seq);
  EXPECT_EQ(seq.currentStep, 1U);
  EXPECT_EQ(seq.status, SequenceStatus::EXECUTING);

  system_core::data::advanceStep(seq);
  EXPECT_EQ(seq.currentStep, 2U);
  EXPECT_EQ(seq.status, SequenceStatus::EXECUTING);

  system_core::data::advanceStep(seq);
  EXPECT_EQ(seq.status, SequenceStatus::COMPLETE);
}

/** @test Action in step carries correct target data through tick. */
TEST(DataSequence, ActionDataPreserved) {
  DataSequence seq{};
  seq.stepCount = 1;
  seq.steps[0].delayCycles = 1;

  DataTarget target{};
  target.fullUid = 0x007800;
  target.category = DataCategory::OUTPUT;
  target.byteOffset = 36;
  target.byteLen = 4;

  float val = 42.0F;
  system_core::data::initSetAction(seq.steps[0].action, target, ActionTrigger::IMMEDIATE, 0, &val,
                                   4);
  seq.armed = true;

  system_core::data::startSequence(seq);

  bool ready = false;
  const DataAction* a = system_core::data::tickSequence(seq, ready);
  EXPECT_TRUE(ready);
  EXPECT_NE(a, nullptr);
  EXPECT_EQ(a->target.fullUid, 0x007800U);
  EXPECT_EQ(a->target.byteOffset, 36U);

  float recovered = 0.0F;
  std::memcpy(&recovered, a->xorMask.data(), sizeof(float));
  EXPECT_FLOAT_EQ(recovered, 42.0F);
}

/** @test Out-of-bounds currentStep transitions to COMPLETE. */
TEST(DataSequence, OutOfBoundsStep) {
  DataSequence seq{};
  seq.stepCount = 2;
  seq.currentStep = 5; // Past end
  seq.status = SequenceStatus::WAITING;

  bool ready = false;
  const DataAction* a = system_core::data::tickSequence(seq, ready);

  EXPECT_EQ(a, nullptr);
  EXPECT_FALSE(ready);
  EXPECT_EQ(seq.status, SequenceStatus::COMPLETE);
}

/** @test ATS type is preserved (no behavioral difference at this level). */
TEST(DataSequence, ATSType) {
  DataSequence seq{};
  seq.type = SequenceType::ATS;
  seq.stepCount = 1;
  seq.steps[0].delayCycles = 100;
  seq.armed = true;

  system_core::data::startSequence(seq);
  EXPECT_EQ(seq.type, SequenceType::ATS);
  EXPECT_EQ(seq.delayRemaining, 100U);
}

/** @test Event-driven trigger: eventId match starts sequence. */
TEST(DataSequence, EventDrivenStart) {
  DataSequence seq{};
  seq.eventId = 7;
  seq.stepCount = 1;
  seq.steps[0].delayCycles = 0;
  seq.armed = true;

  // Simulate event dispatch
  if (system_core::data::shouldTrigger(seq, 7)) {
    system_core::data::startSequence(seq);
  }

  EXPECT_EQ(seq.status, SequenceStatus::EXECUTING);
  EXPECT_EQ(seq.runCount, 1U);
}

/* ----------------------------- Repeat Tests ----------------------------- */

/** @test Default repeatMax is 0 (run once). */
TEST(DataSequence, DefaultRepeatMax) {
  DataSequence seq{};
  EXPECT_EQ(seq.repeatMax, 0U);
  EXPECT_EQ(seq.repeatCount, 0U);
}

/** @test SEQUENCE_REPEAT_FOREVER constant is 0xFF. */
TEST(DataSequence, RepeatForeverConstant) { EXPECT_EQ(SEQUENCE_REPEAT_FOREVER, 0xFF); }

/** @test repeatMax=0 completes after one pass (existing behavior). */
TEST(DataSequence, RepeatZeroRunsOnce) {
  DataSequence seq{};
  seq.stepCount = 1;
  seq.steps[0].delayCycles = 0;
  seq.repeatMax = 0;
  seq.armed = true;

  system_core::data::startSequence(seq);
  EXPECT_EQ(seq.status, SequenceStatus::EXECUTING);

  system_core::data::advanceStep(seq);
  EXPECT_EQ(seq.status, SequenceStatus::COMPLETE);
  EXPECT_EQ(seq.repeatCount, 0U);
  EXPECT_EQ(seq.runCount, 1U);
}

/** @test repeatMax=2 runs 3 times total (1 initial + 2 repeats). */
TEST(DataSequence, RepeatTwoRunsThreeTimes) {
  DataSequence seq{};
  seq.stepCount = 1;
  seq.steps[0].delayCycles = 0;
  seq.repeatMax = 2;
  seq.armed = true;

  system_core::data::startSequence(seq);
  EXPECT_EQ(seq.runCount, 1U);

  // Pass 1: advance past last step -> should loop
  system_core::data::advanceStep(seq);
  EXPECT_EQ(seq.status, SequenceStatus::EXECUTING);
  EXPECT_EQ(seq.currentStep, 0U);
  EXPECT_EQ(seq.repeatCount, 1U);
  EXPECT_EQ(seq.runCount, 2U);

  // Pass 2: advance -> should loop again
  system_core::data::advanceStep(seq);
  EXPECT_EQ(seq.status, SequenceStatus::EXECUTING);
  EXPECT_EQ(seq.currentStep, 0U);
  EXPECT_EQ(seq.repeatCount, 2U);
  EXPECT_EQ(seq.runCount, 3U);

  // Pass 3: advance -> should complete (repeatCount == repeatMax)
  system_core::data::advanceStep(seq);
  EXPECT_EQ(seq.status, SequenceStatus::COMPLETE);
}

/** @test repeatMax=FOREVER loops indefinitely. */
TEST(DataSequence, RepeatForeverLoops) {
  DataSequence seq{};
  seq.stepCount = 1;
  seq.steps[0].delayCycles = 0;
  seq.repeatMax = SEQUENCE_REPEAT_FOREVER;
  seq.armed = true;

  system_core::data::startSequence(seq);

  // Run 10 iterations - should never complete
  for (int i = 0; i < 10; ++i) {
    EXPECT_NE(seq.status, SequenceStatus::COMPLETE);
    system_core::data::advanceStep(seq);
    EXPECT_EQ(seq.currentStep, 0U);
    EXPECT_EQ(seq.status, SequenceStatus::EXECUTING);
  }
  EXPECT_EQ(seq.runCount, 11U); // 1 initial + 10 repeats
}

/** @test Repeat with multi-step sequence. */
TEST(DataSequence, RepeatMultiStep) {
  DataSequence seq{};
  seq.stepCount = 2;
  seq.steps[0].delayCycles = 0;
  seq.steps[1].delayCycles = 0;
  seq.repeatMax = 1; // 2 total passes
  seq.armed = true;

  system_core::data::startSequence(seq);

  // Pass 1, step 0
  EXPECT_EQ(seq.currentStep, 0U);
  system_core::data::advanceStep(seq);
  EXPECT_EQ(seq.currentStep, 1U);

  // Pass 1, step 1 -> loop
  system_core::data::advanceStep(seq);
  EXPECT_EQ(seq.currentStep, 0U);
  EXPECT_EQ(seq.repeatCount, 1U);

  // Pass 2, step 0
  system_core::data::advanceStep(seq);
  EXPECT_EQ(seq.currentStep, 1U);

  // Pass 2, step 1 -> complete
  system_core::data::advanceStep(seq);
  EXPECT_EQ(seq.status, SequenceStatus::COMPLETE);
}

/** @test Reset clears repeatCount. */
TEST(DataSequence, ResetClearsRepeatCount) {
  DataSequence seq{};
  seq.stepCount = 1;
  seq.steps[0].delayCycles = 0;
  seq.repeatMax = 5;
  seq.armed = true;

  system_core::data::startSequence(seq);
  system_core::data::advanceStep(seq); // repeat 1
  system_core::data::advanceStep(seq); // repeat 2
  EXPECT_EQ(seq.repeatCount, 2U);

  system_core::data::resetSequence(seq);
  EXPECT_EQ(seq.repeatCount, 0U);
  EXPECT_EQ(seq.status, SequenceStatus::IDLE);
}

/** @test Repeat with delay on first step. */
TEST(DataSequence, RepeatWithDelay) {
  DataSequence seq{};
  seq.stepCount = 1;
  seq.steps[0].delayCycles = 2;
  seq.repeatMax = 1;
  seq.armed = true;

  system_core::data::startSequence(seq);
  EXPECT_EQ(seq.status, SequenceStatus::WAITING);

  bool ready = false;
  // Tick down delay for pass 1
  (void)system_core::data::tickSequence(seq, ready);
  EXPECT_FALSE(ready);
  (void)system_core::data::tickSequence(seq, ready);
  EXPECT_TRUE(ready);

  // Advance -> loops with delay
  system_core::data::advanceStep(seq);
  EXPECT_EQ(seq.status, SequenceStatus::WAITING);
  EXPECT_EQ(seq.delayRemaining, 2U);
  EXPECT_EQ(seq.currentStep, 0U);
  EXPECT_EQ(seq.repeatCount, 1U);
}

/* ----------------------------- Branching Integration ----------------------------- */

/** @test GOTO_STEP creates a loop within a sequence. */
TEST(DataSequence, GotoStepCreatesLoop) {
  DataSequence seq{};
  seq.stepCount = 3;
  seq.steps[0].delayCycles = 0;
  seq.steps[1].delayCycles = 0;
  seq.steps[2].delayCycles = 0;
  seq.steps[2].onComplete = StepCompletionAction::GOTO_STEP;
  seq.steps[2].gotoStep = 1; // Loop back to step 1
  seq.armed = true;

  system_core::data::startSequence(seq);

  // Step 0 -> step 1
  system_core::data::advanceStep(seq);
  EXPECT_EQ(seq.currentStep, 1U);

  // Step 1 -> step 2
  system_core::data::advanceStep(seq);
  EXPECT_EQ(seq.currentStep, 2U);

  // Step 2 -> goto step 1 (loop)
  system_core::data::advanceStep(seq);
  EXPECT_EQ(seq.currentStep, 1U);
  EXPECT_EQ(seq.status, SequenceStatus::EXECUTING);

  // Still loops
  system_core::data::advanceStep(seq);
  EXPECT_EQ(seq.currentStep, 2U);
  system_core::data::advanceStep(seq);
  EXPECT_EQ(seq.currentStep, 1U);
}
