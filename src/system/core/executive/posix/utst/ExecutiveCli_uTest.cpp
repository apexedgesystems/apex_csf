/**
 * @file ExecutiveCli_uTest.cpp
 * @brief The clock-rate flag: whole-token parsing and the boot's refusal of bad values.
 */

#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/executive/posix/inc/ApexExecutive_CLI.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

using executive::cli::parseWholeU32;

/* ----------------------------- parseWholeU32 ----------------------------- */

/** @test Whole decimal tokens parse; everything else is refused without throwing. */
TEST(ExecutiveCli, ParseWholeU32) {
  std::uint32_t v = 7;
  EXPECT_TRUE(parseWholeU32("600", v));
  EXPECT_EQ(v, 600U);
  EXPECT_TRUE(parseWholeU32("0", v));
  EXPECT_EQ(v, 0U);
  EXPECT_TRUE(parseWholeU32("4294967295", v));
  EXPECT_EQ(v, 4294967295U);

  v = 7;
  EXPECT_FALSE(parseWholeU32("", v));
  EXPECT_FALSE(parseWholeU32("abc", v));
  EXPECT_FALSE(parseWholeU32("600junk", v));
  EXPECT_FALSE(parseWholeU32("600.9", v));
  EXPECT_FALSE(parseWholeU32("1e3", v));
  EXPECT_FALSE(parseWholeU32(" 600", v));
  EXPECT_FALSE(parseWholeU32("+600", v));
  EXPECT_FALSE(parseWholeU32("-1", v));
  EXPECT_FALSE(parseWholeU32("4294967296", v));
  EXPECT_FALSE(parseWholeU32("18446744073709551616", v));
  EXPECT_EQ(v, 7U) << "a failed parse leaves the output untouched";
}

/* ----------------------------- Boot refusal ----------------------------- */

namespace {

std::filesystem::path testRoot(const char* name) {
  const auto ROOT = std::filesystem::path(::testing::TempDir()) / name;
  std::filesystem::remove_all(ROOT);
  std::filesystem::create_directories(ROOT);
  return ROOT;
}

/// A config-less executive given one clock-rate argument; init() runs the
/// argument pass first, so a refused value returns before anything else.
std::uint8_t initWithClockHz(const std::string& token) {
  executive::ApexExecutive exec("ExecutiveCli_uTest", {"--clock-hz", token},
                                testRoot("exec_cli_utest"));
  return exec.init();
}

} // namespace

/** @test Malformed, out-of-range and overflowing clock-rate values refuse the boot instead of
 * aborting. */
TEST(ExecutiveCli, ClockHzRefusesBadTokens) {
  for (const char* BAD :
       {"abc", "", "600junk", "600.9", "1e3", "18446744073709551616", "0", "1001"}) {
    EXPECT_NE(initWithClockHz(BAD), 0U) << "token '" << BAD << "' was accepted";
  }
}

/** @test A valid clock rate boots and is the configured fundamental. */
TEST(ExecutiveCli, ClockHzAcceptsWholeNumberInRange) {
  executive::ApexExecutive exec("ExecutiveCli_uTest", {"--clock-hz", "600", "--skip-cleanup"},
                                testRoot("exec_cli_utest_ok"));
  ASSERT_EQ(exec.init(), 0U);
  EXPECT_EQ(exec.getHealthPacket().clockFrequencyHz, 600U);
}
