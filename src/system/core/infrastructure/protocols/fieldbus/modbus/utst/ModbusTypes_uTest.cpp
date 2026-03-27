/**
 * @file ModbusTypes_uTest.cpp
 * @brief Unit tests for Modbus types, function codes, and constants.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusTypes.hpp"

#include <gtest/gtest.h>

namespace modbus = apex::protocols::fieldbus::modbus;

/* ----------------------------- Constants ----------------------------- */

/** @test Unit address bounds are correct per Modbus spec. */
TEST(ModbusConstantsTest, UnitAddressBounds) {
  EXPECT_EQ(modbus::Constants::UNIT_ADDRESS_MIN, 1);
  EXPECT_EQ(modbus::Constants::UNIT_ADDRESS_MAX, 247);
  EXPECT_EQ(modbus::Constants::UNIT_ADDRESS_BROADCAST, 0);
}

/** @test Register read limits are correct per Modbus spec. */
TEST(ModbusConstantsTest, ReadLimits) {
  EXPECT_EQ(modbus::Constants::MAX_READ_COILS, 2000);
  EXPECT_EQ(modbus::Constants::MAX_READ_REGISTERS, 125);
}

/** @test Register write limits are correct per Modbus spec. */
TEST(ModbusConstantsTest, WriteLimits) {
  EXPECT_EQ(modbus::Constants::MAX_WRITE_COILS, 1968);
  EXPECT_EQ(modbus::Constants::MAX_WRITE_REGISTERS, 123);
}

/** @test Frame size limits are correct. */
TEST(ModbusConstantsTest, FrameSizeLimits) {
  EXPECT_EQ(modbus::Constants::RTU_MIN_FRAME_SIZE, 4); // Unit + FC + CRC
  EXPECT_EQ(modbus::Constants::RTU_MAX_FRAME_SIZE, 256);
  EXPECT_EQ(modbus::Constants::TCP_MIN_FRAME_SIZE, 8); // MBAP + Unit + FC
  EXPECT_EQ(modbus::Constants::TCP_MAX_FRAME_SIZE, 260);
  EXPECT_EQ(modbus::Constants::PDU_MAX_SIZE, 253);
  EXPECT_EQ(modbus::Constants::CRC_SIZE, 2);
}

/** @test MBAP header constants are correct. */
TEST(ModbusConstantsTest, MbapConstants) {
  EXPECT_EQ(modbus::Constants::MBAP_HEADER_SIZE, 7);
  EXPECT_EQ(modbus::Constants::MBAP_PROTOCOL_ID, 0);
}

/* ----------------------------- FunctionCode ----------------------------- */

/** @test Function code values match Modbus specification. */
TEST(ModbusFunctionCodeTest, CodeValuesMatchSpec) {
  EXPECT_EQ(static_cast<std::uint8_t>(modbus::FunctionCode::READ_COILS), 0x01);
  EXPECT_EQ(static_cast<std::uint8_t>(modbus::FunctionCode::READ_DISCRETE_INPUTS), 0x02);
  EXPECT_EQ(static_cast<std::uint8_t>(modbus::FunctionCode::READ_HOLDING_REGISTERS), 0x03);
  EXPECT_EQ(static_cast<std::uint8_t>(modbus::FunctionCode::READ_INPUT_REGISTERS), 0x04);
  EXPECT_EQ(static_cast<std::uint8_t>(modbus::FunctionCode::WRITE_SINGLE_COIL), 0x05);
  EXPECT_EQ(static_cast<std::uint8_t>(modbus::FunctionCode::WRITE_SINGLE_REGISTER), 0x06);
  EXPECT_EQ(static_cast<std::uint8_t>(modbus::FunctionCode::READ_EXCEPTION_STATUS), 0x07);
  EXPECT_EQ(static_cast<std::uint8_t>(modbus::FunctionCode::DIAGNOSTICS), 0x08);
  EXPECT_EQ(static_cast<std::uint8_t>(modbus::FunctionCode::WRITE_MULTIPLE_COILS), 0x0F);
  EXPECT_EQ(static_cast<std::uint8_t>(modbus::FunctionCode::WRITE_MULTIPLE_REGISTERS), 0x10);
  EXPECT_EQ(static_cast<std::uint8_t>(modbus::FunctionCode::READ_WRITE_MULTIPLE_REGISTERS), 0x17);
  EXPECT_EQ(static_cast<std::uint8_t>(modbus::FunctionCode::READ_FIFO_QUEUE), 0x18);
  EXPECT_EQ(static_cast<std::uint8_t>(modbus::FunctionCode::READ_DEVICE_IDENTIFICATION), 0x2B);
}

/** @test toString returns expected strings for all function codes. */
TEST(ModbusFunctionCodeTest, ToStringKnownMappings) {
  EXPECT_STREQ(modbus::toString(modbus::FunctionCode::READ_COILS), "READ_COILS");
  EXPECT_STREQ(modbus::toString(modbus::FunctionCode::READ_DISCRETE_INPUTS),
               "READ_DISCRETE_INPUTS");
  EXPECT_STREQ(modbus::toString(modbus::FunctionCode::READ_HOLDING_REGISTERS),
               "READ_HOLDING_REGISTERS");
  EXPECT_STREQ(modbus::toString(modbus::FunctionCode::READ_INPUT_REGISTERS),
               "READ_INPUT_REGISTERS");
  EXPECT_STREQ(modbus::toString(modbus::FunctionCode::WRITE_SINGLE_COIL), "WRITE_SINGLE_COIL");
  EXPECT_STREQ(modbus::toString(modbus::FunctionCode::WRITE_SINGLE_REGISTER),
               "WRITE_SINGLE_REGISTER");
  EXPECT_STREQ(modbus::toString(modbus::FunctionCode::WRITE_MULTIPLE_COILS),
               "WRITE_MULTIPLE_COILS");
  EXPECT_STREQ(modbus::toString(modbus::FunctionCode::WRITE_MULTIPLE_REGISTERS),
               "WRITE_MULTIPLE_REGISTERS");
}

/** @test toString returns "UNKNOWN" for invalid function codes. */
TEST(ModbusFunctionCodeTest, ToStringUnknownValue) {
  const auto INVALID = static_cast<modbus::FunctionCode>(0xFF);
  EXPECT_STREQ(modbus::toString(INVALID), "UNKNOWN");
}

/* ----------------------------- isReadFunction ----------------------------- */

/** @test isReadFunction returns true for read function codes. */
TEST(ModbusFunctionCodeTest, IsReadFunctionTrue) {
  EXPECT_TRUE(modbus::isReadFunction(modbus::FunctionCode::READ_COILS));
  EXPECT_TRUE(modbus::isReadFunction(modbus::FunctionCode::READ_DISCRETE_INPUTS));
  EXPECT_TRUE(modbus::isReadFunction(modbus::FunctionCode::READ_HOLDING_REGISTERS));
  EXPECT_TRUE(modbus::isReadFunction(modbus::FunctionCode::READ_INPUT_REGISTERS));
  EXPECT_TRUE(modbus::isReadFunction(modbus::FunctionCode::READ_EXCEPTION_STATUS));
  EXPECT_TRUE(modbus::isReadFunction(modbus::FunctionCode::READ_FIFO_QUEUE));
  EXPECT_TRUE(modbus::isReadFunction(modbus::FunctionCode::READ_DEVICE_IDENTIFICATION));
}

/** @test isReadFunction returns false for write function codes. */
TEST(ModbusFunctionCodeTest, IsReadFunctionFalse) {
  EXPECT_FALSE(modbus::isReadFunction(modbus::FunctionCode::WRITE_SINGLE_COIL));
  EXPECT_FALSE(modbus::isReadFunction(modbus::FunctionCode::WRITE_SINGLE_REGISTER));
  EXPECT_FALSE(modbus::isReadFunction(modbus::FunctionCode::WRITE_MULTIPLE_COILS));
  EXPECT_FALSE(modbus::isReadFunction(modbus::FunctionCode::WRITE_MULTIPLE_REGISTERS));
}

/* ----------------------------- isWriteFunction ----------------------------- */

/** @test isWriteFunction returns true for write function codes. */
TEST(ModbusFunctionCodeTest, IsWriteFunctionTrue) {
  EXPECT_TRUE(modbus::isWriteFunction(modbus::FunctionCode::WRITE_SINGLE_COIL));
  EXPECT_TRUE(modbus::isWriteFunction(modbus::FunctionCode::WRITE_SINGLE_REGISTER));
  EXPECT_TRUE(modbus::isWriteFunction(modbus::FunctionCode::WRITE_MULTIPLE_COILS));
  EXPECT_TRUE(modbus::isWriteFunction(modbus::FunctionCode::WRITE_MULTIPLE_REGISTERS));
  EXPECT_TRUE(modbus::isWriteFunction(modbus::FunctionCode::READ_WRITE_MULTIPLE_REGISTERS));
}

/** @test isWriteFunction returns false for read-only function codes. */
TEST(ModbusFunctionCodeTest, IsWriteFunctionFalse) {
  EXPECT_FALSE(modbus::isWriteFunction(modbus::FunctionCode::READ_COILS));
  EXPECT_FALSE(modbus::isWriteFunction(modbus::FunctionCode::READ_HOLDING_REGISTERS));
  EXPECT_FALSE(modbus::isWriteFunction(modbus::FunctionCode::READ_INPUT_REGISTERS));
}

/* ----------------------------- CoilValue ----------------------------- */

/** @test CoilValue constants match Modbus spec. */
TEST(ModbusCoilValueTest, Constants) {
  EXPECT_EQ(modbus::CoilValue::ON, 0xFF00);
  EXPECT_EQ(modbus::CoilValue::OFF, 0x0000);
}

/** @test CoilValue::isValid returns true for valid values. */
TEST(ModbusCoilValueTest, IsValidTrue) {
  EXPECT_TRUE(modbus::CoilValue::isValid(0xFF00));
  EXPECT_TRUE(modbus::CoilValue::isValid(0x0000));
}

/** @test CoilValue::isValid returns false for invalid values. */
TEST(ModbusCoilValueTest, IsValidFalse) {
  EXPECT_FALSE(modbus::CoilValue::isValid(0x0001));
  EXPECT_FALSE(modbus::CoilValue::isValid(0x00FF));
  EXPECT_FALSE(modbus::CoilValue::isValid(0xFFFF));
  EXPECT_FALSE(modbus::CoilValue::isValid(0x1234));
}

/** @test CoilValue::fromBool converts correctly. */
TEST(ModbusCoilValueTest, FromBool) {
  EXPECT_EQ(modbus::CoilValue::fromBool(true), 0xFF00);
  EXPECT_EQ(modbus::CoilValue::fromBool(false), 0x0000);
}

/** @test CoilValue::toBool converts correctly. */
TEST(ModbusCoilValueTest, ToBool) {
  EXPECT_TRUE(modbus::CoilValue::toBool(0xFF00));
  EXPECT_FALSE(modbus::CoilValue::toBool(0x0000));
  EXPECT_FALSE(modbus::CoilValue::toBool(0x1234)); // Invalid value treated as false
}

/* ----------------------------- Address Validation ----------------------------- */

/** @test isValidUnitAddress returns true for valid range. */
TEST(ModbusAddressTest, ValidUnitAddresses) {
  EXPECT_TRUE(modbus::isValidUnitAddress(1));
  EXPECT_TRUE(modbus::isValidUnitAddress(100));
  EXPECT_TRUE(modbus::isValidUnitAddress(247));
}

/** @test isValidUnitAddress returns false for out-of-range values. */
TEST(ModbusAddressTest, InvalidUnitAddresses) {
  EXPECT_FALSE(modbus::isValidUnitAddress(0)); // Broadcast, not valid for unicast
  EXPECT_FALSE(modbus::isValidUnitAddress(248));
  EXPECT_FALSE(modbus::isValidUnitAddress(255));
}

/** @test isBroadcastAddress returns true only for address 0. */
TEST(ModbusAddressTest, BroadcastAddress) {
  EXPECT_TRUE(modbus::isBroadcastAddress(0));
  EXPECT_FALSE(modbus::isBroadcastAddress(1));
  EXPECT_FALSE(modbus::isBroadcastAddress(255));
}
