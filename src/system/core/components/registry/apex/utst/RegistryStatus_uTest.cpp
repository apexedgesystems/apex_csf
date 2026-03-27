/**
 * @file RegistryStatus_uTest.cpp
 * @brief Unit tests for system_core::registry::Status enum.
 *
 * Tests toString() mappings for all known values and verifies ABI compactness.
 */

#include "src/system/core/components/registry/apex/inc/RegistryStatus.hpp"

#include <array>
#include <type_traits>

#include <gtest/gtest.h>

using system_core::registry::isError;
using system_core::registry::isSuccess;
using system_core::registry::isWarning;
using system_core::registry::Status;
using system_core::registry::toString;

/* ----------------------------- Status toString Tests ----------------------------- */

/** @test All known Status values map to the expected string literal. */
TEST(RegistryStatusTest, KnownMappings) {
  struct Case {
    Status s;
    const char* name;
  };
  const std::array<Case, 13> CASES{{
      {Status::SUCCESS, "SUCCESS"},
      {Status::ERROR_ALREADY_FROZEN, "ERROR_ALREADY_FROZEN"},
      {Status::ERROR_NOT_FROZEN, "ERROR_NOT_FROZEN"},
      {Status::ERROR_NULL_POINTER, "ERROR_NULL_POINTER"},
      {Status::ERROR_DUPLICATE_COMPONENT, "ERROR_DUPLICATE_COMPONENT"},
      {Status::ERROR_DUPLICATE_TASK, "ERROR_DUPLICATE_TASK"},
      {Status::ERROR_DUPLICATE_DATA, "ERROR_DUPLICATE_DATA"},
      {Status::ERROR_COMPONENT_NOT_FOUND, "ERROR_COMPONENT_NOT_FOUND"},
      {Status::ERROR_CAPACITY_EXCEEDED, "ERROR_CAPACITY_EXCEEDED"},
      {Status::ERROR_INVALID_CATEGORY, "ERROR_INVALID_CATEGORY"},
      {Status::ERROR_ZERO_SIZE, "ERROR_ZERO_SIZE"},
      {Status::ERROR_NOT_FOUND, "ERROR_NOT_FOUND"},
      {Status::ERROR_IO, "ERROR_IO"},
  }};

  for (const auto& c : CASES) {
    const char* s = toString(c.s);
    EXPECT_NE(s, nullptr);
    EXPECT_STREQ(s, c.name);
  }
}

/** @test Warning status maps correctly. */
TEST(RegistryStatusTest, WarningMapping) {
  const char* s = toString(Status::WARN_EMPTY_NAME);
  EXPECT_NE(s, nullptr);
  EXPECT_STREQ(s, "WARN_EMPTY_NAME");
}

/** @test End-of-enum marker maps to its literal for visibility. */
TEST(RegistryStatusTest, MarkerMapping) {
  const char* s = toString(Status::EOE_REGISTRY);
  EXPECT_NE(s, nullptr);
  EXPECT_STREQ(s, "EOE_REGISTRY");
}

/** @test Out-of-range enum cast yields "UNKNOWN_STATUS" (defensive default). */
TEST(RegistryStatusTest, UnknownMapping) {
  auto bogus = static_cast<Status>(0xFF);
  const char* s = toString(bogus);
  EXPECT_NE(s, nullptr);
  EXPECT_STREQ(s, "UNKNOWN_STATUS");
}

/** @test Enum remains compact (uint8_t) to keep ABI & logs tidy. */
TEST(RegistryStatusTest, UnderlyingType) {
  static_assert(std::is_same<std::underlying_type<Status>::type, std::uint8_t>::value,
                "Status must remain uint8_t for ABI/log compactness");
  SUCCEED();
}

/** @test toString must be noexcept per header contract. */
TEST(RegistryStatusTest, NoexceptContract) {
  static_assert(noexcept(toString(Status::SUCCESS)), "toString(Status) should be noexcept");
  SUCCEED();
}

/* ----------------------------- Status Helper Tests ----------------------------- */

/** @test isSuccess returns true only for SUCCESS. */
TEST(RegistryStatusTest, IsSuccess) {
  EXPECT_TRUE(isSuccess(Status::SUCCESS));
  EXPECT_FALSE(isSuccess(Status::ERROR_NULL_POINTER));
  EXPECT_FALSE(isSuccess(Status::WARN_EMPTY_NAME));
}

/** @test isError returns true for all ERROR_* statuses. */
TEST(RegistryStatusTest, IsError) {
  EXPECT_TRUE(isError(Status::ERROR_ALREADY_FROZEN));
  EXPECT_TRUE(isError(Status::ERROR_NOT_FROZEN));
  EXPECT_TRUE(isError(Status::ERROR_NULL_POINTER));
  EXPECT_TRUE(isError(Status::ERROR_DUPLICATE_COMPONENT));
  EXPECT_TRUE(isError(Status::ERROR_DUPLICATE_TASK));
  EXPECT_TRUE(isError(Status::ERROR_DUPLICATE_DATA));
  EXPECT_TRUE(isError(Status::ERROR_COMPONENT_NOT_FOUND));
  EXPECT_TRUE(isError(Status::ERROR_CAPACITY_EXCEEDED));
  EXPECT_TRUE(isError(Status::ERROR_INVALID_CATEGORY));
  EXPECT_TRUE(isError(Status::ERROR_ZERO_SIZE));
  EXPECT_TRUE(isError(Status::ERROR_NOT_FOUND));
  EXPECT_TRUE(isError(Status::ERROR_IO));
  EXPECT_FALSE(isError(Status::SUCCESS));
  EXPECT_FALSE(isError(Status::WARN_EMPTY_NAME));
}

/** @test isWarning returns true only for WARN_* statuses. */
TEST(RegistryStatusTest, IsWarning) {
  EXPECT_TRUE(isWarning(Status::WARN_EMPTY_NAME));
  EXPECT_FALSE(isWarning(Status::SUCCESS));
  EXPECT_FALSE(isWarning(Status::ERROR_NULL_POINTER));
}
