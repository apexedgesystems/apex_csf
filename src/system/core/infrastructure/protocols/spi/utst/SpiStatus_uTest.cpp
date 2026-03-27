/**
 * @file SpiStatus_uTest.cpp
 * @brief Unit tests for SPI status codes.
 */

#include "src/system/core/infrastructure/protocols/spi/inc/SpiStatus.hpp"

#include <cstring>
#include <gtest/gtest.h>
#include <set>

using apex::protocols::spi::Status;
using apex::protocols::spi::toString;

/* ----------------------------- Enum Tests ----------------------------- */

/** @test Verify SUCCESS has value 0. */
TEST(SpiStatus, SuccessIsZero) { EXPECT_EQ(static_cast<int>(Status::SUCCESS), 0); }

/** @test Verify all status codes have unique values. */
TEST(SpiStatus, UniqueValues) {
  std::set<std::uint8_t> values;
  values.insert(static_cast<std::uint8_t>(Status::SUCCESS));
  values.insert(static_cast<std::uint8_t>(Status::WOULD_BLOCK));
  values.insert(static_cast<std::uint8_t>(Status::ERROR_TIMEOUT));
  values.insert(static_cast<std::uint8_t>(Status::ERROR_CLOSED));
  values.insert(static_cast<std::uint8_t>(Status::ERROR_INVALID_ARG));
  values.insert(static_cast<std::uint8_t>(Status::ERROR_NOT_CONFIGURED));
  values.insert(static_cast<std::uint8_t>(Status::ERROR_IO));
  values.insert(static_cast<std::uint8_t>(Status::ERROR_UNSUPPORTED));
  values.insert(static_cast<std::uint8_t>(Status::ERROR_BUSY));

  EXPECT_EQ(values.size(), 9U);
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test Verify toString returns valid strings for all status codes. */
TEST(SpiStatus, ToStringAllCodes) {
  EXPECT_STREQ(toString(Status::SUCCESS), "SUCCESS");
  EXPECT_STREQ(toString(Status::WOULD_BLOCK), "WOULD_BLOCK");
  EXPECT_STREQ(toString(Status::ERROR_TIMEOUT), "ERROR_TIMEOUT");
  EXPECT_STREQ(toString(Status::ERROR_CLOSED), "ERROR_CLOSED");
  EXPECT_STREQ(toString(Status::ERROR_INVALID_ARG), "ERROR_INVALID_ARG");
  EXPECT_STREQ(toString(Status::ERROR_NOT_CONFIGURED), "ERROR_NOT_CONFIGURED");
  EXPECT_STREQ(toString(Status::ERROR_IO), "ERROR_IO");
  EXPECT_STREQ(toString(Status::ERROR_UNSUPPORTED), "ERROR_UNSUPPORTED");
  EXPECT_STREQ(toString(Status::ERROR_BUSY), "ERROR_BUSY");
}

/** @test Verify toString handles unknown value gracefully. */
TEST(SpiStatus, ToStringUnknown) {
  auto unknown = static_cast<Status>(255);
  EXPECT_STREQ(toString(unknown), "UNKNOWN");
}

/** @test Verify toString returns non-null for all codes. */
TEST(SpiStatus, ToStringNonNull) {
  for (int i = 0; i < 20; ++i) {
    const char* str = toString(static_cast<Status>(i));
    EXPECT_NE(str, nullptr);
    EXPECT_GT(std::strlen(str), 0U);
  }
}
