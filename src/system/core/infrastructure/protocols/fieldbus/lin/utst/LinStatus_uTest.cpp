/**
 * @file LinStatus_uTest.cpp
 * @brief Unit tests for LinStatus enum and toString().
 */

#include "src/system/core/infrastructure/protocols/fieldbus/lin/inc/LinStatus.hpp"

#include <gtest/gtest.h>

#include <cstring>

using apex::protocols::fieldbus::lin::Status;
using apex::protocols::fieldbus::lin::toString;

/* ----------------------------- Enum Values ----------------------------- */

/** @test SUCCESS has value 0. */
TEST(LinStatusTest, SuccessIsZero) { EXPECT_EQ(static_cast<std::uint8_t>(Status::SUCCESS), 0); }

/** @test All status values are distinct. */
TEST(LinStatusTest, AllValuesDistinct) {
  const std::uint8_t VALUES[] = {
      static_cast<std::uint8_t>(Status::SUCCESS),
      static_cast<std::uint8_t>(Status::WOULD_BLOCK),
      static_cast<std::uint8_t>(Status::ERROR_TIMEOUT),
      static_cast<std::uint8_t>(Status::ERROR_CLOSED),
      static_cast<std::uint8_t>(Status::ERROR_INVALID_ARG),
      static_cast<std::uint8_t>(Status::ERROR_NOT_CONFIGURED),
      static_cast<std::uint8_t>(Status::ERROR_IO),
      static_cast<std::uint8_t>(Status::ERROR_CHECKSUM),
      static_cast<std::uint8_t>(Status::ERROR_SYNC),
      static_cast<std::uint8_t>(Status::ERROR_PARITY),
      static_cast<std::uint8_t>(Status::ERROR_FRAME),
      static_cast<std::uint8_t>(Status::ERROR_NO_RESPONSE),
      static_cast<std::uint8_t>(Status::ERROR_BUS_COLLISION),
      static_cast<std::uint8_t>(Status::ERROR_BREAK),
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
TEST(LinStatusTest, ToStringKnownMappings) {
  EXPECT_STREQ(toString(Status::SUCCESS), "SUCCESS");
  EXPECT_STREQ(toString(Status::WOULD_BLOCK), "WOULD_BLOCK");
  EXPECT_STREQ(toString(Status::ERROR_TIMEOUT), "ERROR_TIMEOUT");
  EXPECT_STREQ(toString(Status::ERROR_CLOSED), "ERROR_CLOSED");
  EXPECT_STREQ(toString(Status::ERROR_INVALID_ARG), "ERROR_INVALID_ARG");
  EXPECT_STREQ(toString(Status::ERROR_NOT_CONFIGURED), "ERROR_NOT_CONFIGURED");
  EXPECT_STREQ(toString(Status::ERROR_IO), "ERROR_IO");
  EXPECT_STREQ(toString(Status::ERROR_CHECKSUM), "ERROR_CHECKSUM");
  EXPECT_STREQ(toString(Status::ERROR_SYNC), "ERROR_SYNC");
  EXPECT_STREQ(toString(Status::ERROR_PARITY), "ERROR_PARITY");
  EXPECT_STREQ(toString(Status::ERROR_FRAME), "ERROR_FRAME");
  EXPECT_STREQ(toString(Status::ERROR_NO_RESPONSE), "ERROR_NO_RESPONSE");
  EXPECT_STREQ(toString(Status::ERROR_BUS_COLLISION), "ERROR_BUS_COLLISION");
  EXPECT_STREQ(toString(Status::ERROR_BREAK), "ERROR_BREAK");
}

/** @test toString returns non-null for all known values. */
TEST(LinStatusTest, ToStringNeverNull) {
  EXPECT_NE(toString(Status::SUCCESS), nullptr);
  EXPECT_NE(toString(Status::WOULD_BLOCK), nullptr);
  EXPECT_NE(toString(Status::ERROR_TIMEOUT), nullptr);
  EXPECT_NE(toString(Status::ERROR_CLOSED), nullptr);
  EXPECT_NE(toString(Status::ERROR_INVALID_ARG), nullptr);
  EXPECT_NE(toString(Status::ERROR_NOT_CONFIGURED), nullptr);
  EXPECT_NE(toString(Status::ERROR_IO), nullptr);
  EXPECT_NE(toString(Status::ERROR_CHECKSUM), nullptr);
  EXPECT_NE(toString(Status::ERROR_SYNC), nullptr);
  EXPECT_NE(toString(Status::ERROR_PARITY), nullptr);
  EXPECT_NE(toString(Status::ERROR_FRAME), nullptr);
  EXPECT_NE(toString(Status::ERROR_NO_RESPONSE), nullptr);
  EXPECT_NE(toString(Status::ERROR_BUS_COLLISION), nullptr);
  EXPECT_NE(toString(Status::ERROR_BREAK), nullptr);
}

/** @test toString returns "UNKNOWN" for invalid values. */
TEST(LinStatusTest, ToStringUnknownValue) {
  const auto INVALID = static_cast<Status>(0xFF);
  EXPECT_STREQ(toString(INVALID), "UNKNOWN");
}
