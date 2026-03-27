/**
 * @file CanStatus_uTest.cpp
 * @brief Unit tests for CAN Status → string mapping.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/CanStatus.hpp"

#include <gtest/gtest.h>
#include <array>
#include <type_traits> // underlying_type, noexcept checks

using apex::protocols::fieldbus::can::Status;
using apex::protocols::fieldbus::can::toString;

/** @test All known Status values map to the expected string literal. */
TEST(CanStatusTest, KnownMappings) {
  struct Case {
    Status s;
    const char* name;
  };
  const std::array<Case, 8> CASES{{
      {Status::SUCCESS, "SUCCESS"},
      {Status::WOULD_BLOCK, "WOULD_BLOCK"},
      {Status::ERROR_TIMEOUT, "ERROR_TIMEOUT"},
      {Status::ERROR_CLOSED, "ERROR_CLOSED"},
      {Status::ERROR_INVALID_ARG, "ERROR_INVALID_ARG"},
      {Status::ERROR_NOT_CONFIGURED, "ERROR_NOT_CONFIGURED"},
      {Status::ERROR_IO, "ERROR_IO"},
      {Status::ERROR_UNSUPPORTED, "ERROR_UNSUPPORTED"},
  }};

  for (const auto& c : CASES) {
    // Arrange / Act
    const char* s = toString(c.s);
    // Assert
    EXPECT_NE(s, nullptr);
    EXPECT_STREQ(s, c.name);
  }
}

/** @test Out-of-range enum cast yields "UNKNOWN_STATUS" (defensive default). */
TEST(CanStatusTest, UnknownMapping) {
  auto bogus = static_cast<Status>(0xFF);
  const char* s = toString(bogus);
  EXPECT_NE(s, nullptr);
  EXPECT_STREQ(s, "UNKNOWN_STATUS");
}

/** @test Enum is compact (uint8_t) to keep ABI & logs tidy. */
TEST(CanStatusTest, UnderlyingType) {
  static_assert(std::is_same<std::underlying_type<Status>::type, std::uint8_t>::value,
                "Status must remain uint8_t for ABI/log compactness");
  SUCCEED();
}

/** @test toString must be noexcept per header contract. */
TEST(CanStatusTest, NoexceptContract) {
  static_assert(noexcept(toString(Status::SUCCESS)), "toString(Status) should be noexcept");
  SUCCEED();
}
