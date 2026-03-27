/**
 * @file UartStatus_uTest.cpp
 * @brief Unit tests for UART Status enum and toString mapping.
 */

#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartStatus.hpp"

#include <gtest/gtest.h>
#include <array>
#include <type_traits>

using apex::protocols::serial::uart::Status;
using apex::protocols::serial::uart::toString;

/* ----------------------------- Enum Tests ----------------------------- */

/** @test All known Status values map to the expected string literal. */
TEST(UartStatusTest, KnownMappings) {
  struct Case {
    Status s;
    const char* name;
  };
  const std::array<Case, 9> CASES{{
      {Status::SUCCESS, "SUCCESS"},
      {Status::WOULD_BLOCK, "WOULD_BLOCK"},
      {Status::ERROR_TIMEOUT, "ERROR_TIMEOUT"},
      {Status::ERROR_CLOSED, "ERROR_CLOSED"},
      {Status::ERROR_INVALID_ARG, "ERROR_INVALID_ARG"},
      {Status::ERROR_NOT_CONFIGURED, "ERROR_NOT_CONFIGURED"},
      {Status::ERROR_IO, "ERROR_IO"},
      {Status::ERROR_UNSUPPORTED, "ERROR_UNSUPPORTED"},
      {Status::ERROR_BUSY, "ERROR_BUSY"},
  }};

  for (const auto& c : CASES) {
    const char* s = toString(c.s);
    EXPECT_NE(s, nullptr);
    EXPECT_STREQ(s, c.name);
  }
}

/** @test Out-of-range enum cast yields "UNKNOWN_STATUS" (defensive default). */
TEST(UartStatusTest, UnknownMapping) {
  auto bogus = static_cast<Status>(0xFF);
  const char* s = toString(bogus);
  EXPECT_NE(s, nullptr);
  EXPECT_STREQ(s, "UNKNOWN_STATUS");
}

/** @test Enum is compact (uint8_t) to keep ABI and logs tidy. */
TEST(UartStatusTest, UnderlyingType) {
  static_assert(std::is_same<std::underlying_type<Status>::type, std::uint8_t>::value,
                "Status must remain uint8_t for ABI/log compactness");
  SUCCEED();
}

/** @test toString must be noexcept per header contract. */
TEST(UartStatusTest, NoexceptContract) {
  static_assert(noexcept(toString(Status::SUCCESS)), "toString(Status) should be noexcept");
  SUCCEED();
}
