/**
 * @file DataAction_uTest.cpp
 * @brief Unit tests for DataAction struct and helper functions.
 */

#include "src/system/core/infrastructure/data/inc/DataAction.hpp"

#include <gtest/gtest.h>

#include <cstring>

using system_core::data::ActionStatus;
using system_core::data::ActionTrigger;
using system_core::data::ActionType;
using system_core::data::ArmControlTarget;
using system_core::data::COMMAND_PAYLOAD_MAX;
using system_core::data::DataAction;
using system_core::data::DataCategory;
using system_core::data::DataTarget;
using system_core::data::initArmControlAction;
using system_core::data::initCommandAction;
using system_core::data::initFlipAction;
using system_core::data::initHighAction;
using system_core::data::initSetAction;
using system_core::data::initZeroAction;
using system_core::data::isActive;
using system_core::data::isArmControl;
using system_core::data::isCommand;
using system_core::data::isExpired;
using system_core::data::isPending;
using system_core::data::toString;

/* ----------------------------- Enum Tests ----------------------------- */

/** @test ActionTrigger enum has expected values. */
TEST(DataAction, TriggerEnumValues) {
  EXPECT_EQ(static_cast<std::uint8_t>(ActionTrigger::IMMEDIATE), 0);
  EXPECT_EQ(static_cast<std::uint8_t>(ActionTrigger::AT_CYCLE), 1);
  EXPECT_EQ(static_cast<std::uint8_t>(ActionTrigger::AFTER_CYCLES), 2);
  EXPECT_EQ(static_cast<std::uint8_t>(ActionTrigger::AT_TIME), 3);
  EXPECT_EQ(static_cast<std::uint8_t>(ActionTrigger::ON_EVENT), 4);
}

/** @test ActionTrigger toString covers all values. */
TEST(DataAction, TriggerToString) {
  EXPECT_STREQ(toString(ActionTrigger::IMMEDIATE), "IMMEDIATE");
  EXPECT_STREQ(toString(ActionTrigger::AT_CYCLE), "AT_CYCLE");
  EXPECT_STREQ(toString(ActionTrigger::AFTER_CYCLES), "AFTER_CYCLES");
  EXPECT_STREQ(toString(ActionTrigger::AT_TIME), "AT_TIME");
  EXPECT_STREQ(toString(ActionTrigger::ON_EVENT), "ON_EVENT");
}

/** @test ActionStatus enum has expected values. */
TEST(DataAction, StatusEnumValues) {
  EXPECT_EQ(static_cast<std::uint8_t>(ActionStatus::SUCCESS), 0);
  EXPECT_EQ(static_cast<std::uint8_t>(ActionStatus::ERROR_FULL), 1);
  EXPECT_EQ(static_cast<std::uint8_t>(ActionStatus::PENDING), 5);
  EXPECT_EQ(static_cast<std::uint8_t>(ActionStatus::ACTIVE), 6);
  EXPECT_EQ(static_cast<std::uint8_t>(ActionStatus::EXPIRED), 7);
}

/** @test ActionStatus toString covers all values. */
TEST(DataAction, StatusToString) {
  EXPECT_STREQ(toString(ActionStatus::SUCCESS), "SUCCESS");
  EXPECT_STREQ(toString(ActionStatus::ERROR_FULL), "ERROR_FULL");
  EXPECT_STREQ(toString(ActionStatus::ERROR_PARAM), "ERROR_PARAM");
  EXPECT_STREQ(toString(ActionStatus::ERROR_BOUNDS), "ERROR_BOUNDS");
  EXPECT_STREQ(toString(ActionStatus::ERROR_TARGET), "ERROR_TARGET");
  EXPECT_STREQ(toString(ActionStatus::PENDING), "PENDING");
  EXPECT_STREQ(toString(ActionStatus::ACTIVE), "ACTIVE");
  EXPECT_STREQ(toString(ActionStatus::EXPIRED), "EXPIRED");
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default-constructed DataAction has expected defaults. */
TEST(DataAction, DefaultConstruction) {
  const DataAction A{};
  EXPECT_EQ(A.target.fullUid, 0U);
  EXPECT_EQ(A.trigger, ActionTrigger::IMMEDIATE);
  EXPECT_EQ(A.status, ActionStatus::SUCCESS);
  EXPECT_EQ(A.triggerParam, 0U);
  EXPECT_EQ(A.duration, 0U);
  EXPECT_EQ(A.cyclesRemaining, 0U);
}

/** @test ACTION_QUEUE_SIZE constant is 16. */
TEST(DataAction, QueueSizeConstant) { EXPECT_EQ(system_core::data::ACTION_QUEUE_SIZE, 16U); }

/* ----------------------------- initZeroAction ----------------------------- */

/** @test initZeroAction sets AND=0x00 and XOR=0x00. */
TEST(DataAction, InitZeroAction) {
  DataAction action{};
  const DataTarget TARGET{0x007A00, DataCategory::OUTPUT, 8, 4};

  initZeroAction(action, TARGET, ActionTrigger::AT_CYCLE, 500, 100);

  EXPECT_EQ(action.target, TARGET);
  EXPECT_EQ(action.trigger, ActionTrigger::AT_CYCLE);
  EXPECT_EQ(action.status, ActionStatus::PENDING);
  EXPECT_EQ(action.triggerParam, 500U);
  EXPECT_EQ(action.duration, 100U);
  EXPECT_EQ(action.cyclesRemaining, 100U);

  for (std::uint8_t i = 0; i < 4; ++i) {
    EXPECT_EQ(action.andMask[i], 0x00) << "AND mask byte " << static_cast<int>(i);
    EXPECT_EQ(action.xorMask[i], 0x00) << "XOR mask byte " << static_cast<int>(i);
  }
}

/* ----------------------------- initHighAction ----------------------------- */

/** @test initHighAction sets AND=0x00 and XOR=0xFF. */
TEST(DataAction, InitHighAction) {
  DataAction action{};
  const DataTarget TARGET{0x007800, DataCategory::STATE, 0, 8};

  initHighAction(action, TARGET, ActionTrigger::IMMEDIATE, 0, 0);

  EXPECT_EQ(action.trigger, ActionTrigger::IMMEDIATE);
  EXPECT_EQ(action.duration, 0U);

  for (std::uint8_t i = 0; i < 8; ++i) {
    EXPECT_EQ(action.andMask[i], 0x00) << "AND mask byte " << static_cast<int>(i);
    EXPECT_EQ(action.xorMask[i], 0xFF) << "XOR mask byte " << static_cast<int>(i);
  }
}

/* ----------------------------- initFlipAction ----------------------------- */

/** @test initFlipAction sets AND=0xFF and XOR=0xFF. */
TEST(DataAction, InitFlipAction) {
  DataAction action{};
  const DataTarget TARGET{0x007800, DataCategory::OUTPUT, 36, 4};

  initFlipAction(action, TARGET, ActionTrigger::AFTER_CYCLES, 10, 50);

  EXPECT_EQ(action.trigger, ActionTrigger::AFTER_CYCLES);
  EXPECT_EQ(action.triggerParam, 10U);
  EXPECT_EQ(action.duration, 50U);

  for (std::uint8_t i = 0; i < 4; ++i) {
    EXPECT_EQ(action.andMask[i], 0xFF) << "AND mask byte " << static_cast<int>(i);
    EXPECT_EQ(action.xorMask[i], 0xFF) << "XOR mask byte " << static_cast<int>(i);
  }
}

/* ----------------------------- initSetAction ----------------------------- */

/** @test initSetAction sets AND=0x00 and XOR=value. */
TEST(DataAction, InitSetAction) {
  DataAction action{};
  const DataTarget TARGET{0x007800, DataCategory::OUTPUT, 36, 4};
  const float VALUE = 42.0F;

  initSetAction(action, TARGET, ActionTrigger::AT_TIME, 2500, &VALUE, 4, 0);

  EXPECT_EQ(action.trigger, ActionTrigger::AT_TIME);
  EXPECT_EQ(action.triggerParam, 2500U);
  EXPECT_EQ(action.duration, 0U);

  for (std::uint8_t i = 0; i < 4; ++i) {
    EXPECT_EQ(action.andMask[i], 0x00);
  }

  float recovered = 0.0F;
  std::memcpy(&recovered, action.xorMask.data(), 4);
  EXPECT_FLOAT_EQ(recovered, 42.0F);
}

/** @test initSetAction with null value sets only AND mask. */
TEST(DataAction, InitSetActionNullValue) {
  DataAction action{};
  const DataTarget TARGET{0x007800, DataCategory::STATE, 0, 4};

  initSetAction(action, TARGET, ActionTrigger::IMMEDIATE, 0, nullptr, 4, 0);

  for (std::uint8_t i = 0; i < 4; ++i) {
    EXPECT_EQ(action.andMask[i], 0x00);
  }
}

/* ----------------------------- Status Queries ----------------------------- */

/** @test isPending returns true for PENDING status. */
TEST(DataAction, IsPending) {
  DataAction action{};
  action.status = ActionStatus::PENDING;
  EXPECT_TRUE(isPending(action));
  EXPECT_FALSE(isActive(action));
  EXPECT_FALSE(isExpired(action));
}

/** @test isActive returns true for ACTIVE status. */
TEST(DataAction, IsActive) {
  DataAction action{};
  action.status = ActionStatus::ACTIVE;
  EXPECT_FALSE(isPending(action));
  EXPECT_TRUE(isActive(action));
  EXPECT_FALSE(isExpired(action));
}

/** @test isExpired returns true for EXPIRED status. */
TEST(DataAction, IsExpired) {
  DataAction action{};
  action.status = ActionStatus::EXPIRED;
  EXPECT_FALSE(isPending(action));
  EXPECT_FALSE(isActive(action));
  EXPECT_TRUE(isExpired(action));
}

/* ----------------------------- ON_EVENT Trigger ----------------------------- */

/** @test ON_EVENT trigger stores eventId in triggerParam. */
TEST(DataAction, OnEventTrigger) {
  DataAction action{};
  const DataTarget TARGET{0x007A00, DataCategory::OUTPUT, 0, 16};

  initZeroAction(action, TARGET, ActionTrigger::ON_EVENT, 42, 10);

  EXPECT_EQ(action.trigger, ActionTrigger::ON_EVENT);
  EXPECT_EQ(action.triggerParam, 42U);
}

/* ----------------------------- ActionType Tests ----------------------------- */

/** @test ActionType enum values. */
TEST(DataAction, ActionTypeValues) {
  EXPECT_EQ(static_cast<std::uint8_t>(ActionType::DATA_WRITE), 0);
  EXPECT_EQ(static_cast<std::uint8_t>(ActionType::COMMAND), 1);
}

/** @test ActionType toString. */
TEST(DataAction, ActionTypeToString) {
  EXPECT_STREQ(toString(ActionType::DATA_WRITE), "DATA_WRITE");
  EXPECT_STREQ(toString(ActionType::COMMAND), "COMMAND");
}

/** @test Default action is DATA_WRITE. */
TEST(DataAction, DefaultActionType) {
  const DataAction A{};
  EXPECT_EQ(A.actionType, ActionType::DATA_WRITE);
  EXPECT_FALSE(isCommand(A));
}

/** @test COMMAND_PAYLOAD_MAX constant. */
TEST(DataAction, CommandPayloadMaxConstant) { EXPECT_GE(COMMAND_PAYLOAD_MAX, 8U); }

/* ----------------------------- initCommandAction ----------------------------- */

/** @test initCommandAction sets all command fields correctly. */
TEST(DataAction, InitCommandAction) {
  DataAction action{};
  const std::uint8_t PAYLOAD[] = {0xAA, 0xBB, 0xCC};

  initCommandAction(action, 0x007A00, ActionTrigger::ON_EVENT, 5, 0x01, PAYLOAD, 3);

  EXPECT_EQ(action.target.fullUid, 0x007A00U);
  EXPECT_EQ(action.actionType, ActionType::COMMAND);
  EXPECT_TRUE(isCommand(action));
  EXPECT_EQ(action.trigger, ActionTrigger::ON_EVENT);
  EXPECT_EQ(action.triggerParam, 5U);
  EXPECT_EQ(action.status, ActionStatus::PENDING);
  EXPECT_EQ(action.duration, 0U);
  EXPECT_EQ(action.commandOpcode, 0x01);
  EXPECT_EQ(action.commandPayloadLen, 3U);
  EXPECT_EQ(action.commandPayload[0], 0xAA);
  EXPECT_EQ(action.commandPayload[1], 0xBB);
  EXPECT_EQ(action.commandPayload[2], 0xCC);
}

/** @test initCommandAction with no payload. */
TEST(DataAction, InitCommandNoPayload) {
  DataAction action{};

  initCommandAction(action, 0x006500, ActionTrigger::IMMEDIATE, 0, 0x42);

  EXPECT_TRUE(isCommand(action));
  EXPECT_EQ(action.commandOpcode, 0x42);
  EXPECT_EQ(action.commandPayloadLen, 0U);
}

/** @test initCommandAction with null payload pointer. */
TEST(DataAction, InitCommandNullPayload) {
  DataAction action{};

  initCommandAction(action, 0x006500, ActionTrigger::IMMEDIATE, 0, 0x10, nullptr, 4);

  EXPECT_TRUE(isCommand(action));
  EXPECT_EQ(action.commandPayloadLen, 4U);
  // Payload bytes should remain zero-initialized
  for (std::uint8_t i = 0; i < 4; ++i) {
    EXPECT_EQ(action.commandPayload[i], 0x00);
  }
}

/** @test initCommandAction clamps oversized payload to COMMAND_PAYLOAD_MAX. */
TEST(DataAction, InitCommandPayloadClamp) {
  DataAction action{};
  const std::uint8_t BIG[32] = {};

  initCommandAction(action, 0x006500, ActionTrigger::IMMEDIATE, 0, 0x01, BIG, 32);

  EXPECT_EQ(action.commandPayloadLen, static_cast<std::uint8_t>(COMMAND_PAYLOAD_MAX));
}

/** @test isCommand distinguishes DATA_WRITE from COMMAND. */
TEST(DataAction, IsCommandDistinguishes) {
  DataAction dataWrite{};
  dataWrite.actionType = ActionType::DATA_WRITE;
  EXPECT_FALSE(isCommand(dataWrite));

  DataAction command{};
  command.actionType = ActionType::COMMAND;
  EXPECT_TRUE(isCommand(command));
}

/* ----------------------------- ARM_CONTROL Tests ----------------------------- */

/** @test ARM_CONTROL ActionType enum value. */
TEST(DataAction, ArmControlEnumValue) {
  EXPECT_EQ(static_cast<std::uint8_t>(ActionType::ARM_CONTROL), 2);
}

/** @test ARM_CONTROL toString. */
TEST(DataAction, ArmControlToString) {
  EXPECT_STREQ(toString(ActionType::ARM_CONTROL), "ARM_CONTROL");
}

/** @test ArmControlTarget enum values and toString. */
TEST(DataAction, ArmControlTargetValues) {
  EXPECT_STREQ(toString(ArmControlTarget::WATCHPOINT), "WATCHPOINT");
  EXPECT_STREQ(toString(ArmControlTarget::GROUP), "GROUP");
  EXPECT_STREQ(toString(ArmControlTarget::SEQUENCE), "SEQUENCE");
}

/** @test initArmControlAction sets all fields correctly. */
TEST(DataAction, InitArmControlAction) {
  DataAction action{};

  initArmControlAction(action, ArmControlTarget::WATCHPOINT, 3, true, ActionTrigger::ON_EVENT, 5);

  EXPECT_TRUE(isArmControl(action));
  EXPECT_FALSE(isCommand(action));
  EXPECT_EQ(action.actionType, ActionType::ARM_CONTROL);
  EXPECT_EQ(action.trigger, ActionTrigger::ON_EVENT);
  EXPECT_EQ(action.triggerParam, 5U);
  EXPECT_EQ(action.status, ActionStatus::PENDING);
  EXPECT_EQ(action.armTarget, ArmControlTarget::WATCHPOINT);
  EXPECT_EQ(action.armIndex, 3U);
  EXPECT_TRUE(action.armState);
}

/** @test initArmControlAction for disarming a sequence. */
TEST(DataAction, InitArmControlDisarm) {
  DataAction action{};

  initArmControlAction(action, ArmControlTarget::SEQUENCE, 0, false, ActionTrigger::IMMEDIATE, 0);

  EXPECT_TRUE(isArmControl(action));
  EXPECT_EQ(action.armTarget, ArmControlTarget::SEQUENCE);
  EXPECT_EQ(action.armIndex, 0U);
  EXPECT_FALSE(action.armState);
}

/** @test Layered fault detection: warning arms critical watchpoint. */
TEST(DataAction, LayeredFaultDetectionScenario) {
  // Watchpoint A (warning) fires eventId=1
  // ARM_CONTROL action on eventId=1 arms watchpoint B (critical) at index 2

  DataAction armCritical{};
  initArmControlAction(armCritical, ArmControlTarget::WATCHPOINT, 2, true, ActionTrigger::ON_EVENT,
                       1);

  EXPECT_TRUE(isArmControl(armCritical));
  EXPECT_EQ(armCritical.triggerParam, 1U); // eventId=1
  EXPECT_EQ(armCritical.armTarget, ArmControlTarget::WATCHPOINT);
  EXPECT_EQ(armCritical.armIndex, 2U);
  EXPECT_TRUE(armCritical.armState);
}
