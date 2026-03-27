/**
 * @file SystemComponentStatus_uTest.cpp
 * @brief Unit tests for system_core::system_component::Status enum.
 *
 * Notes:
 *  - Tests toString() mappings for all known values.
 *  - Verifies ABI compactness (uint8_t).
 */

#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"

#include <gtest/gtest.h>

#include <array>
#include <type_traits>

using system_core::system_component::Status;
using system_core::system_component::toString;

/* ----------------------------- Status Tests ----------------------------- */

/** @test All known Status values map to the expected string literal. */
TEST(SystemComponentStatusTest, KnownMappings) {
  struct Case {
    Status s;
    const char* name;
  };
  const std::array<Case, 8> CASES{{
      {Status::SUCCESS, "SUCCESS"},
      {Status::WARN_NOOP, "WARN_NOOP"},
      {Status::ERROR_PARAM, "ERROR_PARAM"},
      {Status::ERROR_ALREADY_INITIALIZED, "ERROR_ALREADY_INITIALIZED"},
      {Status::ERROR_NOT_INITIALIZED, "ERROR_NOT_INITIALIZED"},
      {Status::ERROR_NOT_CONFIGURED, "ERROR_NOT_CONFIGURED"},
      {Status::ERROR_LOAD_INVALID, "ERROR_LOAD_INVALID"},
      {Status::ERROR_CONFIG_APPLY_FAIL, "ERROR_CONFIG_APPLY_FAIL"},
  }};

  for (const auto& c : CASES) {
    // Arrange / Act
    const char* s = toString(c.s);
    // Assert
    EXPECT_NE(s, nullptr);
    EXPECT_STREQ(s, c.name);
  }
}

/** @test End-of-enum marker maps to its literal for visibility. */
TEST(SystemComponentStatusTest, MarkerMapping) {
  const char* s = toString(Status::EOE_SYSTEM_COMPONENT);
  EXPECT_NE(s, nullptr);
  EXPECT_STREQ(s, "EOE_SYSTEM_COMPONENT");
}

/** @test Out-of-range enum cast yields "UNKNOWN_STATUS" (defensive default). */
TEST(SystemComponentStatusTest, UnknownMapping) {
  auto bogus = static_cast<Status>(0xFF);
  const char* s = toString(bogus);
  EXPECT_NE(s, nullptr);
  EXPECT_STREQ(s, "UNKNOWN_STATUS");
}

/** @test Enum remains compact (uint8_t) to keep ABI & logs tidy. */
TEST(SystemComponentStatusTest, UnderlyingType) {
  static_assert(std::is_same<std::underlying_type<Status>::type, std::uint8_t>::value,
                "Status must remain uint8_t for ABI/log compactness");
  SUCCEED();
}

/** @test toString must be noexcept per header contract. */
TEST(SystemComponentStatusTest, NoexceptContract) {
  static_assert(noexcept(toString(Status::SUCCESS)), "toString(Status) should be noexcept");
  SUCCEED();
}
