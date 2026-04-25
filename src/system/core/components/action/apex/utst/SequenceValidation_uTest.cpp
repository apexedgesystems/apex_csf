/**
 * @file SequenceValidation_uTest.cpp
 * @brief Unit tests for sequence pre-flight validation.
 */

#include "src/system/core/components/action/apex/inc/SequenceValidation.hpp"

#include <gtest/gtest.h>

#include <cstring>

using system_core::data::ActionType;
using system_core::data::DataSequence;
using system_core::data::SEQUENCE_MAX_STEPS;
using system_core::data::SEQUENCE_TABLE_SIZE;
using system_core::data::SequenceError;
using system_core::data::SequenceErrorCode;
using system_core::data::SequenceType;
using system_core::data::StepCompletionAction;
using system_core::data::StepTimeoutPolicy;

/* ----------------------------- Default Construction ----------------------------- */

/** @test SequenceError defaults to NONE. */
TEST(SequenceValidation, ErrorDefault) {
  SequenceError err{};
  EXPECT_EQ(err.code, SequenceErrorCode::NONE);
  EXPECT_EQ(err.stepIndex, 0U);
}

/** @test SequenceErrorCode toString covers all values. */
TEST(SequenceValidation, ErrorCodeToString) {
  EXPECT_STREQ(system_core::data::toString(SequenceErrorCode::NONE), "NONE");
  EXPECT_STREQ(system_core::data::toString(SequenceErrorCode::STEP_COUNT_OVERFLOW),
               "STEP_COUNT_OVERFLOW");
  EXPECT_STREQ(system_core::data::toString(SequenceErrorCode::GOTO_OUT_OF_BOUNDS),
               "GOTO_OUT_OF_BOUNDS");
  EXPECT_STREQ(system_core::data::toString(SequenceErrorCode::ATS_NOT_MONOTONIC),
               "ATS_NOT_MONOTONIC");
  EXPECT_STREQ(system_core::data::toString(SequenceErrorCode::ATS_TIME_IN_PAST),
               "ATS_TIME_IN_PAST");
  EXPECT_STREQ(system_core::data::toString(SequenceErrorCode::COMMAND_NO_TARGET),
               "COMMAND_NO_TARGET");
}

/* ----------------------------- Valid Sequences ----------------------------- */

/** @test Empty sequence (0 steps) passes validation. */
TEST(SequenceValidation, EmptySequenceValid) {
  DataSequence seq{};
  seq.stepCount = 0;

  SequenceError err{};
  EXPECT_TRUE(system_core::data::validateSequence(seq, &err));
  EXPECT_EQ(err.code, SequenceErrorCode::NONE);
}

/** @test Simple 1-step RTS passes validation. */
TEST(SequenceValidation, SimpleRtsValid) {
  DataSequence seq{};
  seq.stepCount = 1;
  seq.steps[0].delayCycles = 100;
  seq.steps[0].action.actionType = ActionType::COMMAND;
  seq.steps[0].action.target.fullUid = 0x007800;

  SequenceError err{};
  EXPECT_TRUE(system_core::data::validateSequence(seq, &err));
}

/** @test Multi-step COMMAND sequence passes. */
TEST(SequenceValidation, MultiStepCommandValid) {
  DataSequence seq{};
  seq.stepCount = 3;
  for (int i = 0; i < 3; ++i) {
    seq.steps[i].action.actionType = ActionType::COMMAND;
    seq.steps[i].action.target.fullUid = 0x7800;
    seq.steps[i].delayCycles = 100;
  }

  SequenceError err{};
  EXPECT_TRUE(system_core::data::validateSequence(seq, &err));
}

/** @test Null error pointer doesn't crash. */
TEST(SequenceValidation, NullErrorPtr) {
  DataSequence seq{};
  seq.stepCount = 1;
  seq.steps[0].action.actionType = ActionType::COMMAND;
  seq.steps[0].action.target.fullUid = 0x007800;
  EXPECT_TRUE(system_core::data::validateSequence(seq, nullptr));
}

/* ----------------------------- stepCount Overflow ----------------------------- */

/** @test stepCount > SEQUENCE_MAX_STEPS fails. */
TEST(SequenceValidation, StepCountOverflow) {
  DataSequence seq{};
  seq.stepCount = SEQUENCE_MAX_STEPS + 1;

  SequenceError err{};
  EXPECT_FALSE(system_core::data::validateSequence(seq, &err));
  EXPECT_EQ(err.code, SequenceErrorCode::STEP_COUNT_OVERFLOW);
  EXPECT_EQ(err.stepIndex, 0xFF);
}

/* ----------------------------- GOTO Out of Bounds ----------------------------- */

/** @test onTimeout GOTO_STEP with out-of-bounds target fails. */
TEST(SequenceValidation, GotoTimeoutOutOfBounds) {
  DataSequence seq{};
  seq.stepCount = 2;
  seq.steps[0].onTimeout = StepTimeoutPolicy::GOTO_STEP;
  seq.steps[0].gotoStep = 5; // Out of bounds

  SequenceError err{};
  EXPECT_FALSE(system_core::data::validateSequence(seq, &err));
  EXPECT_EQ(err.code, SequenceErrorCode::GOTO_OUT_OF_BOUNDS);
  EXPECT_EQ(err.stepIndex, 0U);
}

/** @test onComplete GOTO_STEP with out-of-bounds target fails. */
TEST(SequenceValidation, GotoCompleteOutOfBounds) {
  DataSequence seq{};
  seq.stepCount = 2;
  seq.steps[0].action.target.fullUid = 0x007800;
  seq.steps[1].action.target.fullUid = 0x007800;
  seq.steps[1].onComplete = StepCompletionAction::GOTO_STEP;
  seq.steps[1].gotoStep = 10;

  SequenceError err{};
  EXPECT_FALSE(system_core::data::validateSequence(seq, &err));
  EXPECT_EQ(err.code, SequenceErrorCode::GOTO_OUT_OF_BOUNDS);
  EXPECT_EQ(err.stepIndex, 1U);
}

/** @test GOTO_STEP with valid in-bounds target passes. */
TEST(SequenceValidation, GotoInBoundsValid) {
  DataSequence seq{};
  seq.stepCount = 4;
  for (int i = 0; i < 4; ++i) {
    seq.steps[i].action.target.fullUid = 0x007800;
  }
  seq.steps[2].onComplete = StepCompletionAction::GOTO_STEP;
  seq.steps[2].gotoStep = 0; // Loop back to start

  SequenceError err{};
  EXPECT_TRUE(system_core::data::validateSequence(seq, &err));
}

/* ----------------------------- START_RTS Out of Bounds ----------------------------- */

/** @test START_RTS with slot >= SEQUENCE_TABLE_SIZE fails. */
TEST(SequenceValidation, StartRtsOutOfBounds) {
  DataSequence seq{};
  seq.stepCount = 1;
  seq.steps[0].onComplete = StepCompletionAction::START_RTS;
  seq.steps[0].gotoStep = static_cast<std::uint8_t>(SEQUENCE_TABLE_SIZE); // Out of bounds

  SequenceError err{};
  EXPECT_FALSE(system_core::data::validateSequence(seq, &err));
  EXPECT_EQ(err.code, SequenceErrorCode::START_RTS_OUT_OF_BOUNDS);
}

/* ----------------------------- COMMAND No Target ----------------------------- */

/** @test COMMAND action with fullUid=0 fails. */
TEST(SequenceValidation, CommandNoTarget) {
  DataSequence seq{};
  seq.stepCount = 1;
  seq.steps[0].action.actionType = ActionType::COMMAND;
  seq.steps[0].action.target.fullUid = 0; // No target

  SequenceError err{};
  EXPECT_FALSE(system_core::data::validateSequence(seq, &err));
  EXPECT_EQ(err.code, SequenceErrorCode::COMMAND_NO_TARGET);
  EXPECT_EQ(err.stepIndex, 0U);
}

/** @test ARM_CONTROL with fullUid=0 is fine (no target address needed). */
TEST(SequenceValidation, ArmControlZeroUidOk) {
  DataSequence seq{};
  seq.stepCount = 1;
  seq.steps[0].action.actionType = ActionType::ARM_CONTROL;
  seq.steps[0].action.target.fullUid = 0;

  SequenceError err{};
  EXPECT_TRUE(system_core::data::validateSequence(seq, &err));
}

/* ----------------------------- Wait Condition No Target ----------------------------- */

/** @test Enabled wait condition with fullUid=0 fails. */
TEST(SequenceValidation, WaitConditionNoTarget) {
  DataSequence seq{};
  seq.stepCount = 1;
  seq.steps[0].action.actionType = ActionType::ARM_CONTROL;
  seq.steps[0].waitCondition.enabled = true;
  seq.steps[0].waitCondition.target.fullUid = 0;

  SequenceError err{};
  EXPECT_FALSE(system_core::data::validateSequence(seq, &err));
  EXPECT_EQ(err.code, SequenceErrorCode::WAIT_CONDITION_NO_TARGET);
}

/** @test Disabled wait condition with fullUid=0 is fine. */
TEST(SequenceValidation, WaitConditionDisabledOk) {
  DataSequence seq{};
  seq.stepCount = 1;
  seq.steps[0].action.actionType = ActionType::ARM_CONTROL;
  seq.steps[0].waitCondition.enabled = false;
  seq.steps[0].waitCondition.target.fullUid = 0;

  SequenceError err{};
  EXPECT_TRUE(system_core::data::validateSequence(seq, &err));
}

/* ----------------------------- ATS Monotonicity ----------------------------- */

/** @test ATS with non-monotonic delays fails. */
TEST(SequenceValidation, AtsNotMonotonic) {
  DataSequence seq{};
  seq.type = SequenceType::ATS;
  seq.stepCount = 3;
  for (int i = 0; i < 3; ++i) {
    seq.steps[i].action.target.fullUid = 0x007800;
  }
  seq.steps[0].delayCycles = 100;
  seq.steps[1].delayCycles = 500;
  seq.steps[2].delayCycles = 200; // Goes backward

  SequenceError err{};
  EXPECT_FALSE(system_core::data::validateSequence(seq, &err));
  EXPECT_EQ(err.code, SequenceErrorCode::ATS_NOT_MONOTONIC);
  EXPECT_EQ(err.stepIndex, 2U);
}

/** @test ATS with monotonically increasing delays passes. */
TEST(SequenceValidation, AtsMonotonicValid) {
  DataSequence seq{};
  seq.type = SequenceType::ATS;
  seq.stepCount = 3;
  for (int i = 0; i < 3; ++i) {
    seq.steps[i].action.target.fullUid = 0x007800;
  }
  seq.steps[0].delayCycles = 100;
  seq.steps[1].delayCycles = 500;
  seq.steps[2].delayCycles = 5000;

  SequenceError err{};
  EXPECT_TRUE(system_core::data::validateSequence(seq, &err));
}

/** @test ATS with equal delays passes (not strictly increasing required). */
TEST(SequenceValidation, AtsEqualDelaysValid) {
  DataSequence seq{};
  seq.type = SequenceType::ATS;
  seq.stepCount = 2;
  seq.steps[0].action.target.fullUid = 0x007800;
  seq.steps[1].action.target.fullUid = 0x007800;
  seq.steps[0].delayCycles = 100;
  seq.steps[1].delayCycles = 100; // Same time (simultaneous commands)

  SequenceError err{};
  EXPECT_TRUE(system_core::data::validateSequence(seq, &err));
}

/** @test RTS with decreasing delays is fine (relative, not absolute). */
TEST(SequenceValidation, RtsDecreasingDelaysOk) {
  DataSequence seq{};
  seq.type = SequenceType::RTS;
  seq.stepCount = 3;
  for (int i = 0; i < 3; ++i) {
    seq.steps[i].action.target.fullUid = 0x007800;
  }
  seq.steps[0].delayCycles = 1000;
  seq.steps[1].delayCycles = 100;
  seq.steps[2].delayCycles = 10;

  SequenceError err{};
  EXPECT_TRUE(system_core::data::validateSequence(seq, &err));
}

/* ----------------------------- ATS Timeline (Past) ----------------------------- */

/** @test ATS step in the past is rejected. */
TEST(SequenceValidation, AtsTimeInPast) {
  DataSequence seq{};
  seq.type = SequenceType::ATS;
  seq.stepCount = 2;
  seq.steps[0].delayCycles = 100;
  seq.steps[1].delayCycles = 500;

  // Current time is 1000, starting now (startTime=1000).
  // Step 0 fires at 1100 (ok), step 1 fires at 1500 (ok).
  SequenceError err{};
  EXPECT_TRUE(system_core::data::validateAtsTimeline(seq, 1000, 1000, &err));

  // Stale plan: was built for startTime=100 but current time is 1000.
  // Step 0 fires at 200 < 1000 (in the past!).
  EXPECT_FALSE(system_core::data::validateAtsTimeline(seq, 1000, 100, &err));
  EXPECT_EQ(err.code, SequenceErrorCode::ATS_TIME_IN_PAST);
  EXPECT_EQ(err.stepIndex, 0U);

  // Edge case: last step barely in the past.
  // startTime=600, step 1 target = 600+500 = 1100 > 1000 (ok).
  // step 0 target = 600+100 = 700 < 1000 (past!).
  EXPECT_FALSE(system_core::data::validateAtsTimeline(seq, 1000, 600, &err));
  EXPECT_EQ(err.stepIndex, 0U);
}

/** @test ATS with all steps in future passes. */
TEST(SequenceValidation, AtsAllFutureValid) {
  DataSequence seq{};
  seq.type = SequenceType::ATS;
  seq.stepCount = 3;
  seq.steps[0].delayCycles = 100;
  seq.steps[1].delayCycles = 200;
  seq.steps[2].delayCycles = 300;

  SequenceError err{};
  // Starting at current time, all offsets are positive -> all in future
  EXPECT_TRUE(system_core::data::validateAtsTimeline(seq, 5000, 5000, &err));
}

/** @test RTS skips timeline validation. */
TEST(SequenceValidation, RtsSkipsTimeline) {
  DataSequence seq{};
  seq.type = SequenceType::RTS;
  seq.stepCount = 1;
  seq.steps[0].delayCycles = 0;

  SequenceError err{};
  EXPECT_TRUE(system_core::data::validateAtsTimeline(seq, 999999, 0, &err));
}
