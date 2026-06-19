/**
 * @file AtmosphereStatus_uTest.cpp
 * @brief Unit tests for the atmosphere Status code helpers.
 *
 * Exercises toString() for every enumerator and the isSuccess / isWarning /
 * isError classification so the error-reporting surface is verified, not just
 * the SUCCESS path the model tests happen to hit.
 */

#include "src/sim/environment/atmosphere/inc/AtmosphereStatus.hpp"

#include <gtest/gtest.h>

#include <cstring>

namespace {

using sim::environment::atmosphere::isError;
using sim::environment::atmosphere::isSuccess;
using sim::environment::atmosphere::isWarning;
using sim::environment::atmosphere::Status;
using sim::environment::atmosphere::toString;

TEST(AtmosphereStatus, ToStringIsNonEmptyForEveryCode) {
  const Status all[] = {
      Status::SUCCESS,
      Status::WARN_OUT_OF_VALID_RANGE,
      Status::WARN_VACUUM_QUERY,
      Status::ERROR_NOT_INITIALIZED,
      Status::ERROR_DATA_PATH_INVALID,
      Status::ERROR_FILE_FORMAT_INVALID,
      Status::ERROR_MODEL_TYPE_MISMATCH,
      Status::ERROR_PARAM_RHO_INVALID,
      Status::ERROR_PARAM_TEMP_INVALID,
      Status::ERROR_PARAM_PRESSURE_INVALID,
      Status::ERROR_PARAM_SCALE_INVALID,
      Status::ERROR_PARAM_GAS_CONST_INVALID,
      Status::ERROR_PARAM_LAYERS_EMPTY,
      Status::ERROR_PARAM_LAYERS_NONMONOTONIC,
      Status::ERROR_ALLOC_FAIL,
      Status::ERROR_PARAM_BUFFER_NULL,
      Status::ERROR_PARAM_ALT_INVALID,
      Status::EOE_ATMOSPHERE,
  };
  for (const Status s : all) {
    const char* str = toString(s);
    ASSERT_NE(str, nullptr);
    EXPECT_GT(std::strlen(str), 0U);
  }
  EXPECT_STREQ(toString(Status::SUCCESS), "SUCCESS");
  EXPECT_STREQ(toString(Status::WARN_VACUUM_QUERY), "WARN_VACUUM_QUERY");
  EXPECT_STREQ(toString(Status::ERROR_PARAM_LAYERS_NONMONOTONIC),
               "ERROR_PARAM_LAYERS_NONMONOTONIC");
}

TEST(AtmosphereStatus, UnknownCodeMapsToUnknownString) {
  // A value past the defined range exercises the switch's default arm.
  const Status bogus = static_cast<Status>(0xEE);
  EXPECT_STREQ(toString(bogus), "UNKNOWN_STATUS");
}

TEST(AtmosphereStatus, Classification) {
  EXPECT_TRUE(isSuccess(Status::SUCCESS));
  EXPECT_FALSE(isWarning(Status::SUCCESS));
  EXPECT_FALSE(isError(Status::SUCCESS));

  EXPECT_TRUE(isWarning(Status::WARN_OUT_OF_VALID_RANGE));
  EXPECT_TRUE(isWarning(Status::WARN_VACUUM_QUERY));
  EXPECT_FALSE(isError(Status::WARN_VACUUM_QUERY));
  EXPECT_FALSE(isSuccess(Status::WARN_VACUUM_QUERY));

  EXPECT_TRUE(isError(Status::ERROR_NOT_INITIALIZED));
  EXPECT_TRUE(isError(Status::ERROR_MODEL_TYPE_MISMATCH));
  EXPECT_TRUE(isError(Status::ERROR_PARAM_BUFFER_NULL));
  EXPECT_FALSE(isWarning(Status::ERROR_ALLOC_FAIL));

  // The end-of-enum marker is neither success, warning, nor error.
  EXPECT_FALSE(isSuccess(Status::EOE_ATMOSPHERE));
  EXPECT_FALSE(isWarning(Status::EOE_ATMOSPHERE));
  EXPECT_FALSE(isError(Status::EOE_ATMOSPHERE));
}

} // namespace
