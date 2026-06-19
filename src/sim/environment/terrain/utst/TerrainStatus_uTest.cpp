/**
 * @file TerrainStatus_uTest.cpp
 * @brief Unit tests for the terrain Status code helpers.
 *
 * Exercises toString() for every enumerator and the isSuccess / isWarning /
 * isError classification so the error-reporting surface is verified, not just
 * the SUCCESS path the model tests happen to hit.
 */

#include "src/sim/environment/terrain/inc/TerrainStatus.hpp"

#include <gtest/gtest.h>

#include <cstring>

namespace {

using sim::environment::terrain::isError;
using sim::environment::terrain::isSuccess;
using sim::environment::terrain::isWarning;
using sim::environment::terrain::Status;
using sim::environment::terrain::toString;

TEST(TerrainStatus, ToStringIsNonEmptyForEveryCode) {
  const Status all[] = {
      Status::SUCCESS,
      Status::WARN_OUTSIDE_COVERAGE,
      Status::WARN_VOID_DATA,
      Status::ERROR_NOT_INITIALIZED,
      Status::ERROR_DATA_PATH_INVALID,
      Status::ERROR_FILE_FORMAT_INVALID,
      Status::ERROR_SAMPLE_TYPE_UNSUPPORTED,
      Status::ERROR_ALLOC_FAIL,
      Status::ERROR_PARAM_BUFFER_NULL,
      Status::EOE_TERRAIN,
  };
  for (const Status s : all) {
    const char* str = toString(s);
    ASSERT_NE(str, nullptr);
    EXPECT_GT(std::strlen(str), 0U);
  }
  EXPECT_STREQ(toString(Status::SUCCESS), "SUCCESS");
  EXPECT_STREQ(toString(Status::WARN_VOID_DATA), "WARN_VOID_DATA");
  EXPECT_STREQ(toString(Status::ERROR_FILE_FORMAT_INVALID), "ERROR_FILE_FORMAT_INVALID");
}

TEST(TerrainStatus, UnknownCodeMapsToUnknownString) {
  // A value past the defined range exercises the switch's default arm.
  const Status bogus = static_cast<Status>(0xEE);
  EXPECT_STREQ(toString(bogus), "UNKNOWN_STATUS");
}

TEST(TerrainStatus, Classification) {
  EXPECT_TRUE(isSuccess(Status::SUCCESS));
  EXPECT_FALSE(isWarning(Status::SUCCESS));
  EXPECT_FALSE(isError(Status::SUCCESS));

  EXPECT_TRUE(isWarning(Status::WARN_OUTSIDE_COVERAGE));
  EXPECT_TRUE(isWarning(Status::WARN_VOID_DATA));
  EXPECT_FALSE(isError(Status::WARN_VOID_DATA));
  EXPECT_FALSE(isSuccess(Status::WARN_VOID_DATA));

  EXPECT_TRUE(isError(Status::ERROR_NOT_INITIALIZED));
  EXPECT_TRUE(isError(Status::ERROR_FILE_FORMAT_INVALID));
  EXPECT_TRUE(isError(Status::ERROR_PARAM_BUFFER_NULL));
  EXPECT_FALSE(isWarning(Status::ERROR_ALLOC_FAIL));

  // The end-of-enum marker is neither success, warning, nor error.
  EXPECT_FALSE(isSuccess(Status::EOE_TERRAIN));
  EXPECT_FALSE(isWarning(Status::EOE_TERRAIN));
  EXPECT_FALSE(isError(Status::EOE_TERRAIN));
}

} // namespace
