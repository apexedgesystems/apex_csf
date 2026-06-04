/**
 * @file ExecutiveStatus_uTest.cpp
 * @brief Unit tests for ExecutiveStatus enum and helper functions.
 */

#include "src/system/core/executive/posix/inc/ExecutiveStatus.hpp"

#include <cstring>

#include <gtest/gtest.h>

/* ----------------------------- Success Tests ----------------------------- */

/** @test SUCCESS has value 0. */
TEST(ExecutiveStatus, SuccessValueIsZero) {
  EXPECT_EQ(static_cast<std::uint8_t>(executive::Status::SUCCESS), 0);
}

/** @test isSuccess returns true for SUCCESS. */
TEST(ExecutiveStatus, IsSuccessReturnsTrueForSuccess) {
  EXPECT_TRUE(executive::isSuccess(executive::Status::SUCCESS));
}

/** @test isSuccess returns false for error codes. */
TEST(ExecutiveStatus, IsSuccessReturnsFalseForErrors) {
  EXPECT_FALSE(executive::isSuccess(executive::Status::ERROR_MODULE_INIT_FAIL));
  EXPECT_FALSE(executive::isSuccess(executive::Status::ERROR_RUNTIME_FAILURE));
}

/** @test isSuccess returns false for warning codes. */
TEST(ExecutiveStatus, IsSuccessReturnsFalseForWarnings) {
  EXPECT_FALSE(executive::isSuccess(executive::Status::WARN_CLOCK_DRIFT));
  EXPECT_FALSE(executive::isSuccess(executive::Status::WARN_FRAME_OVERRUN));
}

/* ----------------------------- Error Tests ----------------------------- */

/** @test isError returns true for all ERROR_* codes. */
TEST(ExecutiveStatus, IsErrorReturnsTrueForErrorCodes) {
  EXPECT_TRUE(executive::isError(executive::Status::ERROR_MODULE_INIT_FAIL));
  EXPECT_TRUE(executive::isError(executive::Status::ERROR_ARG_PARSE_FAIL));
  EXPECT_TRUE(executive::isError(executive::Status::ERROR_RUNTIME_FAILURE));
  EXPECT_TRUE(executive::isError(executive::Status::ERROR_SHUTDOWN_FAILURE));
  EXPECT_TRUE(executive::isError(executive::Status::ERROR_HARD_REALTIME_FAILURE));
  EXPECT_TRUE(executive::isError(executive::Status::ERROR_COMPONENT_COLLISION));
  EXPECT_TRUE(executive::isError(executive::Status::ERROR_SIGNAL_BLOCK_FAILED));
  EXPECT_TRUE(executive::isError(executive::Status::ERROR_SCHEDULER_NO_TASKS));
  EXPECT_TRUE(executive::isError(executive::Status::ERROR_CONFIG_NOT_FOUND));
  EXPECT_TRUE(executive::isError(executive::Status::ERROR_TPRM_UNPACK_FAIL));
}

/** @test isError returns false for SUCCESS. */
TEST(ExecutiveStatus, IsErrorReturnsFalseForSuccess) {
  EXPECT_FALSE(executive::isError(executive::Status::SUCCESS));
}

/** @test isError returns false for warning codes. */
TEST(ExecutiveStatus, IsErrorReturnsFalseForWarnings) {
  EXPECT_FALSE(executive::isError(executive::Status::WARN_CLOCK_DRIFT));
  EXPECT_FALSE(executive::isError(executive::Status::WARN_IO_ERROR));
}

/* ----------------------------- Warning Tests ----------------------------- */

/** @test isWarning returns true for all WARN_* codes. */
TEST(ExecutiveStatus, IsWarningReturnsTrueForWarningCodes) {
  EXPECT_TRUE(executive::isWarning(executive::Status::WARN_CLOCK_DRIFT));
  EXPECT_TRUE(executive::isWarning(executive::Status::WARN_FRAME_OVERRUN));
  EXPECT_TRUE(executive::isWarning(executive::Status::WARN_REGISTRY_EXPORT));
  EXPECT_TRUE(executive::isWarning(executive::Status::WARN_CLOCK_FROZEN));
  EXPECT_TRUE(executive::isWarning(executive::Status::WARN_CLOCK_STOP_TIMEOUT));
  EXPECT_TRUE(executive::isWarning(executive::Status::WARN_TASK_DRAIN_TIMEOUT));
  EXPECT_TRUE(executive::isWarning(executive::Status::WARN_IO_ERROR));
  EXPECT_TRUE(executive::isWarning(executive::Status::WARN_STARTUP_TIME_PASSED));
  EXPECT_TRUE(executive::isWarning(executive::Status::WARN_INVALID_CLOCK_FREQ));
  EXPECT_TRUE(executive::isWarning(executive::Status::WARN_THREAD_CONFIG_FAIL));
  EXPECT_TRUE(executive::isWarning(executive::Status::WARN_TPRM_LOAD_FAIL));
  EXPECT_TRUE(executive::isWarning(executive::Status::WARN_SWAP_FAILED));
  EXPECT_TRUE(executive::isWarning(executive::Status::WARN_QUEUE_ALLOC_FAIL));
}

/** @test isWarning returns false for SUCCESS. */
TEST(ExecutiveStatus, IsWarningReturnsFalseForSuccess) {
  EXPECT_FALSE(executive::isWarning(executive::Status::SUCCESS));
}

/** @test isWarning returns false for error codes. */
TEST(ExecutiveStatus, IsWarningReturnsFalseForErrors) {
  EXPECT_FALSE(executive::isWarning(executive::Status::ERROR_MODULE_INIT_FAIL));
  EXPECT_FALSE(executive::isWarning(executive::Status::ERROR_RUNTIME_FAILURE));
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test toString returns correct string for SUCCESS. */
TEST(ExecutiveStatus, ToStringSuccess) {
  EXPECT_STREQ(executive::toString(executive::Status::SUCCESS), "SUCCESS");
}

/** @test toString returns correct strings for error codes. */
TEST(ExecutiveStatus, ToStringErrorCodes) {
  EXPECT_STREQ(executive::toString(executive::Status::ERROR_MODULE_INIT_FAIL),
               "ERROR_MODULE_INIT_FAIL");
  EXPECT_STREQ(executive::toString(executive::Status::ERROR_RUNTIME_FAILURE),
               "ERROR_RUNTIME_FAILURE");
  EXPECT_STREQ(executive::toString(executive::Status::ERROR_HARD_REALTIME_FAILURE),
               "ERROR_HARD_REALTIME_FAILURE");
  EXPECT_STREQ(executive::toString(executive::Status::ERROR_COMPONENT_COLLISION),
               "ERROR_COMPONENT_COLLISION");
  EXPECT_STREQ(executive::toString(executive::Status::ERROR_SCHEDULER_NO_TASKS),
               "ERROR_SCHEDULER_NO_TASKS");
}

/** @test toString returns correct strings for warning codes. */
TEST(ExecutiveStatus, ToStringWarningCodes) {
  EXPECT_STREQ(executive::toString(executive::Status::WARN_CLOCK_DRIFT), "WARN_CLOCK_DRIFT");
  EXPECT_STREQ(executive::toString(executive::Status::WARN_FRAME_OVERRUN), "WARN_FRAME_OVERRUN");
  EXPECT_STREQ(executive::toString(executive::Status::WARN_IO_ERROR), "WARN_IO_ERROR");
  EXPECT_STREQ(executive::toString(executive::Status::WARN_THREAD_CONFIG_FAIL),
               "WARN_THREAD_CONFIG_FAIL");
  EXPECT_STREQ(executive::toString(executive::Status::WARN_SWAP_FAILED), "WARN_SWAP_FAILED");
  EXPECT_STREQ(executive::toString(executive::Status::WARN_QUEUE_ALLOC_FAIL),
               "WARN_QUEUE_ALLOC_FAIL");
}

/** @test toString returns non-null for all status values. */
TEST(ExecutiveStatus, ToStringNeverReturnsNull) {
  for (std::uint8_t i = 0; i <= static_cast<std::uint8_t>(executive::Status::EOE_EXECUTIVE); ++i) {
    const char* STR = executive::toString(static_cast<executive::Status>(i));
    EXPECT_NE(STR, nullptr);
    EXPECT_GT(std::strlen(STR), 0U);
  }
}

/* ----------------------------- Enum Value Tests ----------------------------- */

/** @test Error codes start after SystemComponent base. */
TEST(ExecutiveStatus, ErrorCodesStartAfterSystemComponentBase) {
  const auto ERR_START = static_cast<std::uint8_t>(executive::Status::ERROR_MODULE_INIT_FAIL);
  const auto SYS_COMP_END =
      static_cast<std::uint8_t>(system_core::system_component::Status::EOE_SYSTEM_COMPONENT);
  EXPECT_EQ(ERR_START, SYS_COMP_END);
}

/** @test EOE_EXECUTIVE is the final marker. */
TEST(ExecutiveStatus, EoeExecutiveIsFinalMarker) {
  const auto EOE = static_cast<std::uint8_t>(executive::Status::EOE_EXECUTIVE);
  const auto LAST_WARN = static_cast<std::uint8_t>(executive::Status::WARN_QUEUE_ALLOC_FAIL);
  EXPECT_EQ(EOE, LAST_WARN + 1);
}
