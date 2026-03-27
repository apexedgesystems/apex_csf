/**
 * @file AprotoStatus_uTest.cpp
 * @brief Unit tests for APROTO status codes.
 *
 * Tests toString, isSuccess, isError, isWarning functions.
 */

#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoStatus.hpp"

#include <gtest/gtest.h>

using system_core::protocols::aproto::isError;
using system_core::protocols::aproto::isSuccess;
using system_core::protocols::aproto::isWarning;
using system_core::protocols::aproto::Status;
using system_core::protocols::aproto::toString;

/* ----------------------------- toString Tests ----------------------------- */

/** @test toString returns non-null for all defined status values. */
TEST(AprotoStatusTest, ToStringNonNull) {
  EXPECT_NE(toString(Status::SUCCESS), nullptr);
  EXPECT_NE(toString(Status::ERROR_INVALID_MAGIC), nullptr);
  EXPECT_NE(toString(Status::ERROR_INVALID_VERSION), nullptr);
  EXPECT_NE(toString(Status::ERROR_INCOMPLETE), nullptr);
  EXPECT_NE(toString(Status::ERROR_PAYLOAD_TOO_LARGE), nullptr);
  EXPECT_NE(toString(Status::ERROR_BUFFER_TOO_SMALL), nullptr);
  EXPECT_NE(toString(Status::ERROR_PAYLOAD_TRUNCATED), nullptr);
  EXPECT_NE(toString(Status::ERROR_CRC_MISMATCH), nullptr);
  EXPECT_NE(toString(Status::ERROR_DECRYPT_FAILED), nullptr);
  EXPECT_NE(toString(Status::ERROR_ENCRYPT_FAILED), nullptr);
  EXPECT_NE(toString(Status::ERROR_INVALID_KEY), nullptr);
  EXPECT_NE(toString(Status::ERROR_MISSING_CRYPTO), nullptr);
  EXPECT_NE(toString(Status::WARN_RESERVED_FLAGS), nullptr);
  EXPECT_NE(toString(Status::EOE_APROTO), nullptr);
}

/** @test toString returns expected strings. */
TEST(AprotoStatusTest, ToStringValues) {
  EXPECT_STREQ(toString(Status::SUCCESS), "SUCCESS");
  EXPECT_STREQ(toString(Status::ERROR_INVALID_MAGIC), "ERROR_INVALID_MAGIC");
  EXPECT_STREQ(toString(Status::ERROR_CRC_MISMATCH), "ERROR_CRC_MISMATCH");
  EXPECT_STREQ(toString(Status::ERROR_DECRYPT_FAILED), "ERROR_DECRYPT_FAILED");
  EXPECT_STREQ(toString(Status::WARN_RESERVED_FLAGS), "WARN_RESERVED_FLAGS");
}

/** @test toString handles unknown status values. */
TEST(AprotoStatusTest, ToStringUnknown) {
  const auto UNKNOWN = static_cast<Status>(255);
  EXPECT_STREQ(toString(UNKNOWN), "UNKNOWN");
}

/* ----------------------------- isSuccess Tests ----------------------------- */

/** @test isSuccess returns true only for SUCCESS. */
TEST(AprotoStatusTest, IsSuccess) {
  EXPECT_TRUE(isSuccess(Status::SUCCESS));
  EXPECT_FALSE(isSuccess(Status::ERROR_INVALID_MAGIC));
  EXPECT_FALSE(isSuccess(Status::ERROR_CRC_MISMATCH));
  EXPECT_FALSE(isSuccess(Status::WARN_RESERVED_FLAGS));
}

/* ----------------------------- isError Tests ----------------------------- */

/** @test isError returns true for all ERROR_* codes. */
TEST(AprotoStatusTest, IsError) {
  EXPECT_FALSE(isError(Status::SUCCESS));
  EXPECT_TRUE(isError(Status::ERROR_INVALID_MAGIC));
  EXPECT_TRUE(isError(Status::ERROR_INVALID_VERSION));
  EXPECT_TRUE(isError(Status::ERROR_INCOMPLETE));
  EXPECT_TRUE(isError(Status::ERROR_PAYLOAD_TOO_LARGE));
  EXPECT_TRUE(isError(Status::ERROR_BUFFER_TOO_SMALL));
  EXPECT_TRUE(isError(Status::ERROR_PAYLOAD_TRUNCATED));
  EXPECT_TRUE(isError(Status::ERROR_CRC_MISMATCH));
  EXPECT_TRUE(isError(Status::ERROR_DECRYPT_FAILED));
  EXPECT_TRUE(isError(Status::ERROR_ENCRYPT_FAILED));
  EXPECT_TRUE(isError(Status::ERROR_INVALID_KEY));
  EXPECT_TRUE(isError(Status::ERROR_MISSING_CRYPTO));
  EXPECT_FALSE(isError(Status::WARN_RESERVED_FLAGS));
}

/* ----------------------------- isWarning Tests ----------------------------- */

/** @test isWarning returns true for all WARN_* codes. */
TEST(AprotoStatusTest, IsWarning) {
  EXPECT_FALSE(isWarning(Status::SUCCESS));
  EXPECT_FALSE(isWarning(Status::ERROR_INVALID_MAGIC));
  EXPECT_TRUE(isWarning(Status::WARN_RESERVED_FLAGS));
  EXPECT_FALSE(isWarning(Status::EOE_APROTO));
}
