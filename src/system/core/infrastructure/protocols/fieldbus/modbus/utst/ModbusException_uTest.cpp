/**
 * @file ModbusException_uTest.cpp
 * @brief Unit tests for Modbus exception codes and helper functions.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusException.hpp"

#include <gtest/gtest.h>

#include <cstring>

using apex::protocols::fieldbus::modbus::ExceptionCode;
using apex::protocols::fieldbus::modbus::extractOriginalFunctionCode;
using apex::protocols::fieldbus::modbus::isExceptionResponse;
using apex::protocols::fieldbus::modbus::isValidExceptionCode;
using apex::protocols::fieldbus::modbus::toDescription;
using apex::protocols::fieldbus::modbus::toString;

/* ----------------------------- Enum Values ----------------------------- */

/** @test Exception code values match Modbus specification. */
TEST(ModbusExceptionTest, CodeValuesMatchSpec) {
  EXPECT_EQ(static_cast<std::uint8_t>(ExceptionCode::NONE), 0x00);
  EXPECT_EQ(static_cast<std::uint8_t>(ExceptionCode::ILLEGAL_FUNCTION), 0x01);
  EXPECT_EQ(static_cast<std::uint8_t>(ExceptionCode::ILLEGAL_DATA_ADDRESS), 0x02);
  EXPECT_EQ(static_cast<std::uint8_t>(ExceptionCode::ILLEGAL_DATA_VALUE), 0x03);
  EXPECT_EQ(static_cast<std::uint8_t>(ExceptionCode::SLAVE_DEVICE_FAILURE), 0x04);
  EXPECT_EQ(static_cast<std::uint8_t>(ExceptionCode::ACKNOWLEDGE), 0x05);
  EXPECT_EQ(static_cast<std::uint8_t>(ExceptionCode::SLAVE_DEVICE_BUSY), 0x06);
  EXPECT_EQ(static_cast<std::uint8_t>(ExceptionCode::NEGATIVE_ACKNOWLEDGE), 0x07);
  EXPECT_EQ(static_cast<std::uint8_t>(ExceptionCode::MEMORY_PARITY_ERROR), 0x08);
  EXPECT_EQ(static_cast<std::uint8_t>(ExceptionCode::GATEWAY_PATH_UNAVAILABLE), 0x0A);
  EXPECT_EQ(static_cast<std::uint8_t>(ExceptionCode::GATEWAY_TARGET_FAILED), 0x0B);
}

/* ----------------------------- toString ----------------------------- */

/** @test All known exception codes map to expected string literals. */
TEST(ModbusExceptionTest, ToStringKnownMappings) {
  EXPECT_STREQ(toString(ExceptionCode::NONE), "NONE");
  EXPECT_STREQ(toString(ExceptionCode::ILLEGAL_FUNCTION), "ILLEGAL_FUNCTION");
  EXPECT_STREQ(toString(ExceptionCode::ILLEGAL_DATA_ADDRESS), "ILLEGAL_DATA_ADDRESS");
  EXPECT_STREQ(toString(ExceptionCode::ILLEGAL_DATA_VALUE), "ILLEGAL_DATA_VALUE");
  EXPECT_STREQ(toString(ExceptionCode::SLAVE_DEVICE_FAILURE), "SLAVE_DEVICE_FAILURE");
  EXPECT_STREQ(toString(ExceptionCode::ACKNOWLEDGE), "ACKNOWLEDGE");
  EXPECT_STREQ(toString(ExceptionCode::SLAVE_DEVICE_BUSY), "SLAVE_DEVICE_BUSY");
  EXPECT_STREQ(toString(ExceptionCode::NEGATIVE_ACKNOWLEDGE), "NEGATIVE_ACKNOWLEDGE");
  EXPECT_STREQ(toString(ExceptionCode::MEMORY_PARITY_ERROR), "MEMORY_PARITY_ERROR");
  EXPECT_STREQ(toString(ExceptionCode::GATEWAY_PATH_UNAVAILABLE), "GATEWAY_PATH_UNAVAILABLE");
  EXPECT_STREQ(toString(ExceptionCode::GATEWAY_TARGET_FAILED), "GATEWAY_TARGET_FAILED");
}

/** @test toString returns "UNKNOWN" for invalid values. */
TEST(ModbusExceptionTest, ToStringUnknownValue) {
  const auto INVALID = static_cast<ExceptionCode>(0xFF);
  EXPECT_STREQ(toString(INVALID), "UNKNOWN");
}

/* ----------------------------- toDescription ----------------------------- */

/** @test toDescription returns non-empty strings for all known codes. */
TEST(ModbusExceptionTest, ToDescriptionNotEmpty) {
  EXPECT_GT(std::strlen(toDescription(ExceptionCode::NONE)), 0u);
  EXPECT_GT(std::strlen(toDescription(ExceptionCode::ILLEGAL_FUNCTION)), 0u);
  EXPECT_GT(std::strlen(toDescription(ExceptionCode::ILLEGAL_DATA_ADDRESS)), 0u);
  EXPECT_GT(std::strlen(toDescription(ExceptionCode::ILLEGAL_DATA_VALUE)), 0u);
  EXPECT_GT(std::strlen(toDescription(ExceptionCode::SLAVE_DEVICE_FAILURE)), 0u);
  EXPECT_GT(std::strlen(toDescription(ExceptionCode::GATEWAY_TARGET_FAILED)), 0u);
}

/* ----------------------------- isValidExceptionCode ----------------------------- */

/** @test Standard exception codes (0x01-0x08) are valid. */
TEST(ModbusExceptionTest, StandardCodesValid) {
  for (std::uint8_t code = 0x01; code <= 0x08; ++code) {
    EXPECT_TRUE(isValidExceptionCode(code)) << "Code 0x" << std::hex << static_cast<int>(code);
  }
}

/** @test Gateway exception codes (0x0A-0x0B) are valid. */
TEST(ModbusExceptionTest, GatewayCodesValid) {
  EXPECT_TRUE(isValidExceptionCode(0x0A));
  EXPECT_TRUE(isValidExceptionCode(0x0B));
}

/** @test Code 0x00 (NONE) is not a valid Modbus exception code. */
TEST(ModbusExceptionTest, ZeroCodeInvalid) { EXPECT_FALSE(isValidExceptionCode(0x00)); }

/** @test Code 0x09 (gap in spec) is invalid. */
TEST(ModbusExceptionTest, GapCodeInvalid) { EXPECT_FALSE(isValidExceptionCode(0x09)); }

/** @test High codes (0x0C+) are invalid. */
TEST(ModbusExceptionTest, HighCodesInvalid) {
  EXPECT_FALSE(isValidExceptionCode(0x0C));
  EXPECT_FALSE(isValidExceptionCode(0xFF));
}

/* ----------------------------- isExceptionResponse ----------------------------- */

/** @test Function codes with high bit set indicate exception response. */
TEST(ModbusExceptionTest, ExceptionResponseDetection) {
  // Normal function codes (high bit clear)
  EXPECT_FALSE(isExceptionResponse(0x01)); // Read Coils
  EXPECT_FALSE(isExceptionResponse(0x03)); // Read Holding Registers
  EXPECT_FALSE(isExceptionResponse(0x10)); // Write Multiple Registers
  EXPECT_FALSE(isExceptionResponse(0x7F)); // Highest without high bit

  // Exception function codes (high bit set)
  EXPECT_TRUE(isExceptionResponse(0x81)); // Exception for FC 0x01
  EXPECT_TRUE(isExceptionResponse(0x83)); // Exception for FC 0x03
  EXPECT_TRUE(isExceptionResponse(0x90)); // Exception for FC 0x10
  EXPECT_TRUE(isExceptionResponse(0xFF)); // Highest with high bit
}

/* ----------------------------- extractOriginalFunctionCode ----------------------------- */

/** @test Extracting original function code clears the high bit. */
TEST(ModbusExceptionTest, ExtractOriginalFunctionCode) {
  EXPECT_EQ(extractOriginalFunctionCode(0x81), 0x01);
  EXPECT_EQ(extractOriginalFunctionCode(0x83), 0x03);
  EXPECT_EQ(extractOriginalFunctionCode(0x90), 0x10);
  EXPECT_EQ(extractOriginalFunctionCode(0xFF), 0x7F);

  // Already normal function code (no change)
  EXPECT_EQ(extractOriginalFunctionCode(0x01), 0x01);
  EXPECT_EQ(extractOriginalFunctionCode(0x03), 0x03);
}
