/**
 * @file ModbusStatus_uTest.cpp
 * @brief Unit tests for ModbusStatus enum and toString().
 */

#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusStatus.hpp"

#include <gtest/gtest.h>

#include <cstring>

using apex::protocols::fieldbus::modbus::Status;
using apex::protocols::fieldbus::modbus::toString;

/* ----------------------------- Enum Values ----------------------------- */

/** @test SUCCESS has value 0. */
TEST(ModbusStatusTest, SuccessIsZero) { EXPECT_EQ(static_cast<std::uint8_t>(Status::SUCCESS), 0); }

/** @test All status values are distinct. */
TEST(ModbusStatusTest, AllValuesDistinct) {
  const std::uint8_t VALUES[] = {
      static_cast<std::uint8_t>(Status::SUCCESS),
      static_cast<std::uint8_t>(Status::WOULD_BLOCK),
      static_cast<std::uint8_t>(Status::ERROR_TIMEOUT),
      static_cast<std::uint8_t>(Status::ERROR_CLOSED),
      static_cast<std::uint8_t>(Status::ERROR_INVALID_ARG),
      static_cast<std::uint8_t>(Status::ERROR_NOT_CONFIGURED),
      static_cast<std::uint8_t>(Status::ERROR_IO),
      static_cast<std::uint8_t>(Status::ERROR_CRC),
      static_cast<std::uint8_t>(Status::ERROR_FRAME),
      static_cast<std::uint8_t>(Status::ERROR_EXCEPTION),
      static_cast<std::uint8_t>(Status::ERROR_UNSUPPORTED),
  };
  constexpr std::size_t COUNT = sizeof(VALUES) / sizeof(VALUES[0]);

  for (std::size_t i = 0; i < COUNT; ++i) {
    for (std::size_t j = i + 1; j < COUNT; ++j) {
      EXPECT_NE(VALUES[i], VALUES[j]) << "Values at index " << i << " and " << j << " are equal";
    }
  }
}

/* ----------------------------- toString ----------------------------- */

/** @test All known Status values map to expected string literals. */
TEST(ModbusStatusTest, ToStringKnownMappings) {
  EXPECT_STREQ(toString(Status::SUCCESS), "SUCCESS");
  EXPECT_STREQ(toString(Status::WOULD_BLOCK), "WOULD_BLOCK");
  EXPECT_STREQ(toString(Status::ERROR_TIMEOUT), "ERROR_TIMEOUT");
  EXPECT_STREQ(toString(Status::ERROR_CLOSED), "ERROR_CLOSED");
  EXPECT_STREQ(toString(Status::ERROR_INVALID_ARG), "ERROR_INVALID_ARG");
  EXPECT_STREQ(toString(Status::ERROR_NOT_CONFIGURED), "ERROR_NOT_CONFIGURED");
  EXPECT_STREQ(toString(Status::ERROR_IO), "ERROR_IO");
  EXPECT_STREQ(toString(Status::ERROR_CRC), "ERROR_CRC");
  EXPECT_STREQ(toString(Status::ERROR_FRAME), "ERROR_FRAME");
  EXPECT_STREQ(toString(Status::ERROR_EXCEPTION), "ERROR_EXCEPTION");
  EXPECT_STREQ(toString(Status::ERROR_UNSUPPORTED), "ERROR_UNSUPPORTED");
}

/** @test toString returns non-null for all known values. */
TEST(ModbusStatusTest, ToStringNeverNull) {
  EXPECT_NE(toString(Status::SUCCESS), nullptr);
  EXPECT_NE(toString(Status::WOULD_BLOCK), nullptr);
  EXPECT_NE(toString(Status::ERROR_TIMEOUT), nullptr);
  EXPECT_NE(toString(Status::ERROR_CLOSED), nullptr);
  EXPECT_NE(toString(Status::ERROR_INVALID_ARG), nullptr);
  EXPECT_NE(toString(Status::ERROR_NOT_CONFIGURED), nullptr);
  EXPECT_NE(toString(Status::ERROR_IO), nullptr);
  EXPECT_NE(toString(Status::ERROR_CRC), nullptr);
  EXPECT_NE(toString(Status::ERROR_FRAME), nullptr);
  EXPECT_NE(toString(Status::ERROR_EXCEPTION), nullptr);
  EXPECT_NE(toString(Status::ERROR_UNSUPPORTED), nullptr);
}

/** @test toString returns "UNKNOWN" for invalid values. */
TEST(ModbusStatusTest, ToStringUnknownValue) {
  const auto INVALID = static_cast<Status>(0xFF);
  EXPECT_STREQ(toString(INVALID), "UNKNOWN");
}
