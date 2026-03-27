/**
 * @file RfcommStatus_uTest.cpp
 * @brief Unit tests for RfcommStatus.
 */

#include "RfcommStatus.hpp"

#include <gtest/gtest.h>

namespace bt = apex::protocols::wireless::bluetooth;

/* ----------------------------- Enum Tests ----------------------------- */

/** @test Verify SUCCESS has value 0. */
TEST(RfcommStatus, SuccessIsZero) { EXPECT_EQ(static_cast<int>(bt::Status::SUCCESS), 0); }

/** @test Verify all status values are distinct. */
TEST(RfcommStatus, ValuesAreDistinct) {
  EXPECT_NE(bt::Status::SUCCESS, bt::Status::WOULD_BLOCK);
  EXPECT_NE(bt::Status::SUCCESS, bt::Status::ERROR_TIMEOUT);
  EXPECT_NE(bt::Status::SUCCESS, bt::Status::ERROR_CLOSED);
  EXPECT_NE(bt::Status::SUCCESS, bt::Status::ERROR_INVALID_ARG);
  EXPECT_NE(bt::Status::SUCCESS, bt::Status::ERROR_NOT_CONFIGURED);
  EXPECT_NE(bt::Status::SUCCESS, bt::Status::ERROR_IO);
  EXPECT_NE(bt::Status::SUCCESS, bt::Status::ERROR_CONNECTION_REFUSED);
  EXPECT_NE(bt::Status::SUCCESS, bt::Status::ERROR_HOST_UNREACHABLE);
  EXPECT_NE(bt::Status::SUCCESS, bt::Status::ERROR_ALREADY_CONNECTED);
  EXPECT_NE(bt::Status::SUCCESS, bt::Status::ERROR_NOT_CONNECTED);
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test Verify toString returns valid strings for all status values. */
TEST(RfcommStatus, ToStringReturnsValidStrings) {
  EXPECT_STREQ(bt::toString(bt::Status::SUCCESS), "SUCCESS");
  EXPECT_STREQ(bt::toString(bt::Status::WOULD_BLOCK), "WOULD_BLOCK");
  EXPECT_STREQ(bt::toString(bt::Status::ERROR_TIMEOUT), "ERROR_TIMEOUT");
  EXPECT_STREQ(bt::toString(bt::Status::ERROR_CLOSED), "ERROR_CLOSED");
  EXPECT_STREQ(bt::toString(bt::Status::ERROR_INVALID_ARG), "ERROR_INVALID_ARG");
  EXPECT_STREQ(bt::toString(bt::Status::ERROR_NOT_CONFIGURED), "ERROR_NOT_CONFIGURED");
  EXPECT_STREQ(bt::toString(bt::Status::ERROR_IO), "ERROR_IO");
  EXPECT_STREQ(bt::toString(bt::Status::ERROR_CONNECTION_REFUSED), "ERROR_CONNECTION_REFUSED");
  EXPECT_STREQ(bt::toString(bt::Status::ERROR_HOST_UNREACHABLE), "ERROR_HOST_UNREACHABLE");
  EXPECT_STREQ(bt::toString(bt::Status::ERROR_ALREADY_CONNECTED), "ERROR_ALREADY_CONNECTED");
  EXPECT_STREQ(bt::toString(bt::Status::ERROR_NOT_CONNECTED), "ERROR_NOT_CONNECTED");
}

/** @test Verify toString handles unknown values. */
TEST(RfcommStatus, ToStringHandlesUnknown) {
  auto unknown = static_cast<bt::Status>(255);
  EXPECT_STREQ(bt::toString(unknown), "UNKNOWN");
}

/* ----------------------------- Helper Function Tests ----------------------------- */

/** @test Verify isSuccess helper. */
TEST(RfcommStatus, IsSuccessHelper) {
  EXPECT_TRUE(bt::isSuccess(bt::Status::SUCCESS));
  EXPECT_FALSE(bt::isSuccess(bt::Status::WOULD_BLOCK));
  EXPECT_FALSE(bt::isSuccess(bt::Status::ERROR_IO));
}

/** @test Verify isError helper. */
TEST(RfcommStatus, IsErrorHelper) {
  EXPECT_FALSE(bt::isError(bt::Status::SUCCESS));
  EXPECT_FALSE(bt::isError(bt::Status::WOULD_BLOCK));
  EXPECT_TRUE(bt::isError(bt::Status::ERROR_TIMEOUT));
  EXPECT_TRUE(bt::isError(bt::Status::ERROR_CLOSED));
  EXPECT_TRUE(bt::isError(bt::Status::ERROR_INVALID_ARG));
  EXPECT_TRUE(bt::isError(bt::Status::ERROR_NOT_CONFIGURED));
  EXPECT_TRUE(bt::isError(bt::Status::ERROR_IO));
  EXPECT_TRUE(bt::isError(bt::Status::ERROR_CONNECTION_REFUSED));
  EXPECT_TRUE(bt::isError(bt::Status::ERROR_HOST_UNREACHABLE));
  EXPECT_TRUE(bt::isError(bt::Status::ERROR_ALREADY_CONNECTED));
  EXPECT_TRUE(bt::isError(bt::Status::ERROR_NOT_CONNECTED));
}

/** @test Verify shouldRetry helper. */
TEST(RfcommStatus, ShouldRetryHelper) {
  EXPECT_FALSE(bt::shouldRetry(bt::Status::SUCCESS));
  EXPECT_TRUE(bt::shouldRetry(bt::Status::WOULD_BLOCK));
  EXPECT_FALSE(bt::shouldRetry(bt::Status::ERROR_IO));
}
